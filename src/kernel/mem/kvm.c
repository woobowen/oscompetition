#include "mod.h"

// 内核页表
pgtbl_t kernel_pgtbl;
uint64 kernel_pgtbl_pa = 0;

// 根据pagetable,找到va对应的pte
// 若设置alloc=true 则在PTE无效时尝试申请一个物理页
// 成功返回PTE, 失败返回NULL
// 提示：使用 VA_TO_VPN + PTE_TO_PA + PA_TO_PTE
pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    // 处理 pgtbl 为 NULL 的情况：使用内核页表
    if (pgtbl == NULL) {
        pgtbl = kernel_pgtbl;
    }

    // 检查地址合法性
    if (va >= VA_MAX)
        return NULL;

    pgtbl_t curr = pgtbl;  // 当前正在查看的页表

    // level=2 和 level=1（中间层）
    for (int level = 2; level > 0; level--) {
        int idx = VA_TO_VPN(va, level);   // 获取当前层级的索引
        pte_t *pte = &curr[idx];          // 当前层级的 PTE

        if (*pte & PTE_V) {               // PTE 有效
            if (PTE_CHECK(*pte)) {        // 是中间节点（R/W/X=0）
                uint64 child_pa = PTE_TO_PA(*pte);
                curr = (pgtbl_t)child_pa; // 跳转到下一级页表
            } else {
                return NULL; // 非法：中间节点设置了 R/W/X
            }
        } else {                          // PTE 无效
            if (!alloc) return NULL;  // 不允许分配，直接返回 NULL

            // 允许分配新的页表
            void *pa = pmem_alloc(true);  
            if (!pa) return NULL;
            memset(pa, 0, PGSIZE); 

            uint64 child_ppn = PA_TO_PTE((uint64)pa);
            *pte = child_ppn | PTE_V;     // 设置 PTE 指向新页表

            curr = (pgtbl_t)pa;           // 更新当前页表为新分配的
        }
    }

    // 到达这里说明已经到了 level=0
    // 返回 level-0 的 PTE 指针（不管是否有效）
    int idx = VA_TO_VPN(va, 0);
    return &curr[idx];
}

// 在pgtbl中建立 [va, va + len) -> [pa, pa + len) 的映射
// 本质是找到va在页表对应位置的pte并修改它
// 检查: va pa 应当是 page-aligned, len(字节数) > 0, va + len <= VA_MAX
// 注意: perm 应该如何使用
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    //参数检查
    if (len == 0)  panic("vm_mappages: len is zero");  // len(字节数) > 0
    if (va % PGSIZE != 0 || pa % PGSIZE != 0)  panic("vm_mappages: va or pa not page-aligned");  // page-aligned
    if (va + len > VA_MAX)  panic("vm_mappages: virtual address overflow"); // va + len <= VA_MAX

    //逐页映射
    uint64 end = va + len; 
    while (va < end) {
        // Step 1: 获取当前虚拟地址对应的 PTE 指针(如果路径不存在，自动创建中间页表)
        pte_t *pte = vm_getpte(pgtbl, va, true);
        if (!pte) { panic("vm_mappages: cannot create PTE (out of memory?)"); }

        // Step 2: 修改 PTE：将物理地址 pa 编码为 PPN 字段，并加上权限和 V 标志
        *pte = PA_TO_PTE(pa) | perm | PTE_V;

        // Step 3: 前进到下一页
        va += PGSIZE;
        pa += PGSIZE;
    }

    // 页表修改后刷新TLB，避免陈旧映射导致的内存一致性问题
    sfence_vma();

}

// 解除pgtbl中[va, va+len)区域的映射
// 如果freeit == true则释放对应物理页, 默认是用户的物理页
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    //参数检查
    if (len == 0)  panic("vm_unmappages: len is zero");  // len(字节数) > 0
    if (va % PGSIZE != 0)  panic("vm_unmappages: va not page-aligned"); // page-aligned
    if (va + len > VA_MAX)  panic("vm_unmappages: virtual address overflow"); // va + len <= VA_MAX

    //逐页解除映射
    uint64 end = va + len; 
    while (va < end) {
        // Step 1: 获取当前虚拟地址对应的 PTE 指针(不允许自动创建)
        pte_t *pte = vm_getpte(pgtbl, va, false);
        if (!pte || !(*pte & PTE_V))  panic("vm_unmappages: unmap a not mapped page");

        // Step 2: 如果需要，释放对应的物理页
        if (freeit) {
            uint64 pa = PTE_TO_PA(*pte);
            pmem_free(pa, false);
        }

        // Step 3: 将 PTE 标记为无效（解除映射）
        *pte = 0;

        // Step 4: 前进到下一页
        va += PGSIZE;
    }

    // 页表修改后刷新TLB，避免用户继续使用旧TLB访问已释放的物理页
    sfence_vma();
}

// 完成UART、CLINT、PLIC、内核代码区、内核数据区、可分配区域的页表映射
// 相当于部分填充kernel_pgtbl
void kvm_init()
{
    // === Step 1: 分配根页表（第2级页表）===
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true);  // 从内核区域分配一页
    if (!kernel_pgtbl) {
        panic("kvm_init: cannot allocate root page table");
    }
    memset(kernel_pgtbl, 0, PGSIZE);  // 清零
    // 保存 kernel_pgtbl 的物理地址（pmem_alloc 返回的地址在内核中为物理地址/恒等映射）
    kernel_pgtbl_pa = (uint64)kernel_pgtbl;

    // === Step 2: 恒等映射内核代码和数据区===
    vm_mappages(kernel_pgtbl,
                KERNEL_BASE,
                KERNEL_BASE,           // va = pa
                (uint64)KERNEL_DATA - KERNEL_BASE, // 代码区(KERNEL_BASE ~ KERNEL_DATA)
                PTE_R | PTE_W | PTE_X);  // 可读写执行
    
    vm_mappages(kernel_pgtbl,
                (uint64)KERNEL_DATA,
                (uint64)KERNEL_DATA,    // va = pa
                (uint64)ALLOC_BEGIN- ((uint64)KERNEL_DATA), // 数据区(KERNEL_DATA ~ ALLOC_BEGIN)
                PTE_R | PTE_W);         // 可读写

    // === Step 3: 映射设备（UART/CLINT/PLIC）===
    vm_mappages(kernel_pgtbl,
                UART_BASE,
                UART_BASE,
                PGSIZE,
                PTE_R | PTE_W);  // 不可执行

    vm_mappages(kernel_pgtbl,
                CLINT_BASE,
                CLINT_BASE,
                0x10000,  // 64KB
                PTE_R | PTE_W); // 不可执行

    vm_mappages(kernel_pgtbl,
                PLIC_BASE,
                PLIC_BASE,
                0x4000000,  // ~64MB
                PTE_R | PTE_W); // 不可执行

    // 映射 VirtIO 磁盘设备 MMIO 区域
    vm_mappages(kernel_pgtbl,
                VIRTIO_BASE, 
                VIRTIO_BASE,
                PGSIZE,
                PTE_R | PTE_W);  // 不可执行

    // === Step 4: 映射可用内存区域 [ALLOC_BEGIN, ALLOC_END) ===
    vm_mappages(kernel_pgtbl,
                (uint64)ALLOC_BEGIN,
                (uint64)ALLOC_BEGIN,
                (uint64)ALLOC_END-(uint64)ALLOC_BEGIN,
                PTE_R | PTE_W);  // 不可执行

    // === Step 5: 映射 trampoline 区域 ===
    extern char trampoline[]; //来自 trampoline.S
    vm_mappages(kernel_pgtbl,
                TRAMPOLINE,  // va
                (uint64)trampoline,  //pa
                PGSIZE, // 假设 trampoline 占用 4KB
                PTE_R | PTE_W | PTE_X);  // 可读写执行

    // === Step 6: 映射每个进程的内核栈 (为每个 CPU 分配真实的物理页并映射) ===
    // 遍历所有可能的进程槽位 (N_PROC)，为它们预先分配内核栈
    for (int i = 0; i < N_PROC; i++) {
        void *kstack_pa = pmem_alloc(true);  // 分配物理页
        if (!kstack_pa) {
            panic("kvm_init: cannot allocate physical page for kstack of proc");
        }
        memset(kstack_pa, 0, PGSIZE);  

        // 映射到刚刚分配的物理页
        vm_mappages(kernel_pgtbl, 
                    KSTACK(i),
                    (uint64)kstack_pa,
                    PGSIZE, // 先只映射一页
                    PTE_R | PTE_W);
    }
}

// 每个CPU都需要调用, 从不使用页表切换到使用内核页表
// 切换后需要刷新TLB里面的缓存
void kvm_inithart()
{
    w_satp(MAKE_SATP(kernel_pgtbl));
    sfence_vma();
}

// 输出页表内容(for debug)
void vm_print(pgtbl_t pgtbl)
{
    // 顶级页表，次级页表，低级页表
    pgtbl_t pgtbl_2 = pgtbl, pgtbl_1 = NULL, pgtbl_0 = NULL;
    pte_t pte;

    printf("level-2 pgtbl: pa = %p\n", pgtbl_2);
    for (int i = 0; i < PGSIZE / sizeof(pte_t); i++)
    {
        pte = pgtbl_2[i];
        if (!((pte)&PTE_V))
            continue;
        assert(PTE_CHECK(pte), "vm_print: pte check fail (1)");
        pgtbl_1 = (pgtbl_t)PTE_TO_PA(pte);
        printf(".. level-1 pgtbl %d: pa = %p\n", i, pgtbl_1);

        for (int j = 0; j < PGSIZE / sizeof(pte_t); j++)
        {
            pte = pgtbl_1[j];
            if (!((pte)&PTE_V))
                continue;
            assert(PTE_CHECK(pte), "vm_print: pte check fail (2)");
            pgtbl_0 = (pgtbl_t)PTE_TO_PA(pte);
            printf(".. .. level-0 pgtbl %d: pa = %p\n", j, pgtbl_0);

            for (int k = 0; k < PGSIZE / sizeof(pte_t); k++)
            {
                pte = pgtbl_0[k];
                if (!((pte)&PTE_V))
                    continue;
                assert(!PTE_CHECK(pte), "vm_print: pte check fail (3)");
                printf(".. .. .. physical page %d: pa = %p flags = %d\n", k, (uint64)PTE_TO_PA(pte), (int)PTE_FLAGS(pte));
            }
        }
    }
}
