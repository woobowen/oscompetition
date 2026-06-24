#include "mod.h"

/*--------------------part-1: 关于内核空间<->用户空间的数据传递--------------------*/

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint32 copied = 0; // 记录已拷贝字节数

    while (copied < len) {
        // 获取src对应的PTE和物理地址
        pte_t *pte = vm_getpte(pgtbl, src, false);
        if (pte == NULL || !(*pte & PTE_V)) {
            proc_t *p = myproc();
            if (p != NULL && uvm_mmap_handle_fault(pgtbl, src) == (uint64)-1)
                uvm_ustack_grow(pgtbl, p->ustack_npage, src);
            pte = vm_getpte(pgtbl, src, false);
            if (pte == NULL || !(*pte & PTE_V)) {
                printf("uvm_copyin: invalid user address src=%p\n", (void *)src);
                panic("uvm_copyin: invalid user address");
            }
        }
        uint64 pa = PTE_TO_PA(*pte); 
        // 处理不page-aligned的情况-计算页内偏移和本页可拷贝字节数
        uint64 offset = src % PGSIZE; //! 页内偏移
        uint64 copy_len = PGSIZE - offset; //! 本页剩余可拷贝字节数
        if (copy_len > len - copied) {
            copy_len = len - copied; //! 防止多于实际需要拷贝字节数
        }
        // 执行拷贝并更新计数
        memmove((void *)dst, (void *)(pa + offset), copy_len); 
        src += copy_len;
        dst += copy_len;
        copied += copy_len;
    }
}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint32 copied = 0;

    while (copied < len) {
        pte_t *pte = vm_getpte(pgtbl, dst, false);
        if (pte == NULL || !(*pte & PTE_V)) {
            // 目标用户页未映射: 若落在可增长的栈区则按需增长后重试。
            // uvm_ustack_grow 自带范围保护: dst 不在 (MMAP_END, TRAPFRAME) 时返回 -1, 无副作用。
            proc_t *p = myproc();
            if (p != NULL && uvm_mmap_handle_fault(pgtbl, dst) == (uint64)-1)
                uvm_ustack_grow(pgtbl, p->ustack_npage, dst);
            pte = vm_getpte(pgtbl, dst, false);
            if (pte == NULL || !(*pte & PTE_V)) {
                printf("uvm_copyout: invalid dst=%p syscall=%d a0=%p a1=%p a2=%p\n",
                       (void *)dst, p ? (int)p->tf->a7 : -1,
                       p ? (void *)p->tf->a0 : 0, p ? (void *)p->tf->a1 : 0,
                       p ? (void *)p->tf->a2 : 0);
                panic("uvm_copyout: invalid user address");
            }
        }
        uint64 pa = PTE_TO_PA(*pte);
    
        uint64 offset = dst % PGSIZE;
        uint64 copy_len = PGSIZE - offset;
        if (copy_len > len - copied) {
            copy_len = len - copied;
        }

        memmove((void *)(pa + offset), (void *)src, copy_len);
        src += copy_len;
        dst += copy_len;
        copied += copy_len;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    uint32 copied = 0;

    while (copied < maxlen) {
        pte_t *pte = vm_getpte(pgtbl, src, false);
        if (pte == NULL || !(*pte & PTE_V))  panic("uvm_copyin_str: invalid user address");
        uint64 pa = PTE_TO_PA(*pte);
        
        uint64 offset = src % PGSIZE;
        uint64 copy_len = PGSIZE - offset;
        if (copy_len > maxlen - copied) {
            copy_len = maxlen - copied;
        }
        char *src_ptr = (char *)(pa + offset);
        char *dst_ptr = (char *)dst;
        for (uint32 i = 0; i < copy_len; i++) {
            dst_ptr[i] = src_ptr[i];
            if (src_ptr[i] == '\0') {
                return; // 遇到 '\0' 终止
            }
        }
        src += copy_len;
        dst += copy_len;
        copied += copy_len;
    }
}

static mmap_region_t *uvm_mmap_region_at(proc_t *p, uint64 va)
{
    if (p == NULL)
        return NULL;
    for (mmap_region_t *m = p->mmap; m != NULL; m = m->next) {
        uint64 begin = m->begin;
        uint64 end = begin + (uint64)m->npages * PGSIZE;
        if (va >= begin && va < end)
            return m;
    }
    return NULL;
}

static int uvm_mmap_split_at(proc_t *p, uint64 split)
{
    if (p == NULL || split % PGSIZE != 0)
        return -1;

    for (mmap_region_t *m = p->mmap; m != NULL; m = m->next) {
        uint64 begin = m->begin;
        uint64 end = begin + (uint64)m->npages * PGSIZE;

        if (split <= begin)
            return 0;
        if (split >= end)
            continue;

        mmap_region_t *right = mmap_region_alloc();
        right->begin = split;
        right->npages = (uint32)((end - split) / PGSIZE);
        right->perm = m->perm;
        right->next = m->next;

        m->npages = (uint32)((split - begin) / PGSIZE);
        m->next = right;
        return 0;
    }
    return 0;
}

static void uvm_mmap_merge_adjacent(proc_t *p)
{
    if (p == NULL)
        return;

    for (mmap_region_t *m = p->mmap; m != NULL && m->next != NULL; ) {
        mmap_region_t *next = m->next;
        uint64 end = m->begin + (uint64)m->npages * PGSIZE;
        if (end == next->begin && m->perm == next->perm) {
            m->npages += next->npages;
            m->next = next->next;
            mmap_region_free(next);
            continue;
        }
        m = m->next;
    }
}

// Update user PTE permissions. Lazy mmap pages are allowed before allocation.
int uvm_mprotect(pgtbl_t pgtbl, uint64 begin, uint64 len, int perm)
{
    if (len == 0)
        return 0;
    if (begin % PGSIZE != 0 || begin + len < begin || begin + len > VA_MAX)
        return -1;

    proc_t *p = myproc();
    uint64 end = begin + len;
    for (uint64 va = begin; va < end; va += PGSIZE) {
        pte_t *pte = vm_getpte(pgtbl, va, false);
        if (pte != NULL && (*pte & PTE_V) && !PTE_CHECK(*pte))
            continue;
        if (uvm_mmap_region_at(p, va) == NULL)
            return -1;
    }

    if (uvm_mmap_split_at(p, begin) < 0 || uvm_mmap_split_at(p, end) < 0)
        return -1;

    for (mmap_region_t *m = p ? p->mmap : NULL; m != NULL; m = m->next) {
        uint64 m_begin = m->begin;
        uint64 m_end = m_begin + (uint64)m->npages * PGSIZE;
        if (m_begin >= begin && m_end <= end)
            m->perm = perm;
    }

    for (uint64 va = begin; va < end; va += PGSIZE) {
        pte_t *pte = vm_getpte(pgtbl, va, false);
        if (pte != NULL && (*pte & PTE_V) && !PTE_CHECK(*pte))
            *pte = (*pte & ~(PTE_R | PTE_W | PTE_X)) | perm;
    }
    sfence_vma();
    uvm_mmap_merge_adjacent(p);
    return 0;
}

/*--------------------part-2: mmap_region相关--------------------*/

// 打印以mmap为首的mmap链
// for debug
void uvm_show_mmaplist(mmap_region_t *mmap)
{
    mmap_region_t *tmp = mmap;
    printf("\nalloced mmap_space:\n");
    if (tmp == NULL)
        printf("empty\n");
    while (tmp != NULL)
    {
        printf("alloced mmap_region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

// 两个 mmap_region 区域合并
// 注意: 保留一个 释放一个 不操作 next 指针
// 由uvm_mmap调用
static void mmap_merge(mmap_region_t *mmap_1, mmap_region_t *mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");

    // merge
    if (keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_2->perm = mmap_1->perm;
        mmap_region_free(mmap_1);
    }
}

// 寻找一块足够大的区域(len), 作为 mmap_region
// 由uvm_mmap调用(处理begin==0的情况)
// 成功返回begin, 失败返回0
// 专门处理begin==0的情况！
static uint64 uvm_mmap_find(mmap_region_t *head_mmap, uint64 len, mmap_region_t **p_last_mmap, mmap_region_t **p_tmp_mmap)
{
    uint64 start = MMAP_BEGIN;
    mmap_region_t *prev = NULL;
    mmap_region_t *curr = head_mmap;

    // 遍历mmap链寻找空隙
    while (curr != NULL) {
        // 计算当前空隙大小
        uint64 gap_start = (prev == NULL) ? start : (prev->begin + prev->npages * PGSIZE);
        uint64 gap_end = curr->begin;
        if (gap_end > gap_start && (gap_end - gap_start) >= len) {
            // 找到合适的空隙
            // p_last_mmap和p_tmp_mmap是可选输出参数，为了符合sys_mmap的需求
            if (p_last_mmap) *p_last_mmap = prev;
            if (p_tmp_mmap) *p_tmp_mmap = curr;   
            return gap_start;
        }  
        prev = curr;
        curr = curr->next;
    }
    
    // 检查最后一个节点之后的空隙
    uint64 gap_start = (prev == NULL) ? start : (prev->begin + prev->npages * PGSIZE);
    if (MMAP_END > gap_start && (MMAP_END - gap_start) >= len) {
        if (p_last_mmap) *p_last_mmap = prev;
        if (p_tmp_mmap) *p_tmp_mmap = NULL;   
        return gap_start;
    }

    // 未找到合适空隙
    return 0;
}   

// 在用户页表和进程mmap链里新增mmap区域 [begin, begin + npages * PGSIZE)
// 调用者保证begin是page-aligned的, 页面权限为perm -> uvm_mmap里面不需要检查是否页对齐
// 注意: 如果start==0, 意味着需要内核自主找一块足够大的空间
// 修改为返回起始地址，方便sys_mmap使用
uint64 uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    proc_t *p = myproc();
    mmap_region_t *old_head = p->mmap;
    uint64 len = (uint64)npages * PGSIZE;
    mmap_region_t *prev = NULL;
    mmap_region_t *curr = p->mmap;

    // 1. 确定 begin 地址
    if (begin == 0) { // 处理 begin==0 的情况
        begin = uvm_mmap_find(p->mmap, len, &prev, &curr);
        if (begin == 0) { // 未找到合适空间
            return (uint64)-1;
        }
    } else {
        // 检查 begin 范围
        if (begin < MMAP_BEGIN || begin + len > MMAP_END) {
            return (uint64)-1;
        }

        // 查找 mmap 链表的插入位置：mmap 是按 begin 升序排列的
        while (curr != NULL){
            if (curr->begin >= begin + len) {
            // 找到插入点：prev < new < curr
                break;
            }
            if (curr->begin + curr->npages * PGSIZE > begin) {
            // 与现有区域重叠：不允许！
                return (uint64)-1;
            }
            prev = curr;
            curr = curr->next;
        }
    }

    // 2. 申请新的 mmap_region 节点
    mmap_region_t *node = mmap_region_alloc();
    node->begin = begin;
    node->npages = npages;
    node->perm = perm;
    node->next = curr; 

    // 3. 插入 mmap 链表
    if (prev == NULL) {
        p->mmap = node; 
    } else {
        prev->next = node; 
    }

    // 4. 尝试合并
    // 先向后合并：若后继节点紧邻则合并，保留node
    if (node->next != NULL && node->perm == node->next->perm &&
        node->begin + node->npages * PGSIZE == node->next->begin) {
        mmap_region_t *next_node = node->next; // 暂存即将被合并的节点
        node->next = next_node->next; // 【关键修复】先从链表中摘除 next_node
        mmap_merge(node, next_node, true); // 然后合并并释放 next_node
    }
    // 再向前合并：若前驱节点紧邻则合并，保留前驱节点
    if (prev != NULL && prev->perm == node->perm &&
        prev->begin + prev->npages * PGSIZE == node->begin) {
        prev->next = node->next; // 【关键修复】先从链表中摘除 node
        mmap_merge(prev, node, true); // 然后合并并释放 node
    }
    if (p->mmap != old_head)
        proc_shared_vm_sync_mmap(old_head, p->mmap);
    return begin;
}

uint64 uvm_mmap_handle_fault(pgtbl_t pgtbl, uint64 fault_addr)
{
    proc_t *p = myproc();
    if (p == NULL)
        return (uint64)-1;

    uint64 va = (fault_addr / PGSIZE) * PGSIZE;
    for (mmap_region_t *m = p->mmap; m != NULL; m = m->next) {
        uint64 begin = m->begin;
        uint64 end = begin + (uint64)m->npages * PGSIZE;
        if (va < begin || va >= end)
            continue;

        pte_t *pte = vm_getpte(pgtbl, va, false);
        if (pte != NULL && (*pte & PTE_V))
            return (uint64)-1;

        if (proc_shared_vm_fault_page(va, m->perm) < 0)
            return (uint64)-1;
        pte = vm_getpte(pgtbl, va, false);
        if (pte == NULL || !(*pte & PTE_V))
            return (uint64)-1;
        return va;
    }
    return (uint64)-1;
}

// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
// 失败则panic卡死
void uvm_munmap(uint64 begin, uint32 npages)
{
    proc_t *p = myproc();
    mmap_region_t *old_head = p->mmap;
    uint64 end = begin + (uint64)npages * PGSIZE;

    mmap_region_t *prev = NULL;
    mmap_region_t *curr = p->mmap;

    // 处理每个与 [begin, end) 有交集的节点
    while (curr != NULL) {
        uint64 c_begin = curr->begin;
        uint64 c_end = curr->begin + (uint64)curr->npages * PGSIZE;

        if (c_begin < end && c_end > begin) {
            // 交集区间：[o_begin, o_end)
            uint64 o_begin = (begin > c_begin) ? begin : c_begin;
            uint64 o_end = (end < c_end) ? end : c_end;
            uint32 o_npages = (uint32)((o_end - o_begin) / PGSIZE);

            // 1. 解除交集区间的映射
            for (uint32 i = 0; i < o_npages; i++) {
                uint64 va = o_begin + (uint64)i * PGSIZE;
                pte_t *pte = vm_getpte(p->pgtbl, va, false);
                int is_shm = (pte != NULL && (*pte & PTE_V) && (*pte & PTE_SHM));
                uint64 pa = proc_shared_vm_unmap_page(va);
                if (pa != 0 && !is_shm)
                    pmem_free(pa, false);
            }

            // 2. 根据交集在curr中的位置，处理 curr 节点
            if (o_begin > c_begin && o_end < c_end){
                // 情况A：交集在 curr 中间 -> 分裂成两个节点
                // 保留 curr 的前半部分，创建新节点保存后半部分
                mmap_region_t *new_node = mmap_region_alloc();
                new_node->begin = o_end;
                new_node->npages = (uint32)((c_end - o_end) / PGSIZE);
                new_node->perm = curr->perm;
                new_node->next = curr->next;

                curr->npages = (uint32)((o_begin - c_begin) / PGSIZE);
                curr->next = new_node;

                // 处理完 curr，继续从 new_node 的后继开始遍历
                prev = new_node;
                curr = new_node->next;
            }
            else if (o_begin == c_begin && o_end == c_end) {
                // 情况B：交集覆盖整个 curr -> 删除 curr 节点
                mmap_region_t *to_free = curr;
                if (prev == NULL) {
                    p->mmap = curr->next; 
                } else {
                    prev->next = curr->next; 
                }
                curr = curr->next; // 继续遍历后继节点
                mmap_region_free(to_free);
            }
            else if (o_begin == c_begin) {
                // 情况 C: 交集在 curr 前端 -> 调整 curr 起始地址和页数
                curr->begin = o_end;
                curr->npages = (uint32)((c_end - o_end) / PGSIZE);
                prev = curr;
                curr = curr->next;
            }
            else if (o_end == c_end) {
                // 情况 D: 交集在 curr 后端 -> 调整 curr 页数
                curr->npages = (uint32)((o_begin - c_begin) / PGSIZE);
                prev = curr;
                curr = curr->next;
            }
        } else {
            // 无交集，继续遍历
            prev = curr;
            curr = curr->next;
        }
    }
    if (p->mmap != old_head)
        proc_shared_vm_sync_mmap(old_head, p->mmap);
}

/*------------------part-3: 用户空间heap和stack管理相关------------------*/

static void uvm_unmap_existing_leaf_pages(pgtbl_t pgtbl, uint64 begin, uint64 end)
{
    for (uint64 va = begin; va < end; va += PGSIZE) {
        pte_t *pte = vm_getpte(pgtbl, va, false);
        if (pte != NULL && (*pte & PTE_V) && !PTE_CHECK(*pte))
            vm_unmappages(pgtbl, va, PGSIZE, true);
    }
}

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len, int flag) 
{
    if (len == 0) return cur_heap_top;

    uint64 new_top = cur_heap_top + (uint64)len;
    // 计算页数，向上取整
    uint64 cur_pages = (cur_heap_top + PGSIZE - 1) / PGSIZE;
    uint64 new_pages = (new_top + PGSIZE - 1) / PGSIZE;

    // 边界检查：不要越过 mmap 区域开始
    if (new_pages * PGSIZE > (uint64)SIGTRAMPOLINE) {
        return (uint64)-1;
    }

    // 为每一页分配物理页并映射，使用传入的 flag 参数
    for (uint64 p = cur_pages ; p < new_pages; p++) {
        uint64 va = p * PGSIZE;
        void *pa = pmem_alloc(false);
        if (!pa) {
            uvm_unmap_existing_leaf_pages(pgtbl, cur_pages * PGSIZE, p * PGSIZE);
            return (uint64)-1;
        }
        memset(pa, 0, PGSIZE);
        if (vm_try_mappages(pgtbl, va, (uint64)pa, PGSIZE, flag) < 0) {
            pmem_free((uint64)pa, false);
            uvm_unmap_existing_leaf_pages(pgtbl, cur_pages * PGSIZE, p * PGSIZE);
            return (uint64)-1;
        }
    }

    return new_top; 
}

// 用户堆空间减少, 返回新的堆顶地址
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{
    if (len == 0) return cur_heap_top;

    // 最低堆顶限制（proc_make_first 初始化的值）
    const uint64 min_heap = 2 * PGSIZE;
    if (cur_heap_top <= min_heap) return (uint64)-1; // 已经到最低
    // 计算新的堆顶（若减少太多则到达最低堆顶）
    uint64 new_top = (len > cur_heap_top - min_heap) ? min_heap : (cur_heap_top - (uint64)len);

    // 取整：新堆顶按页对齐（向上取整）
    uint64 cur_pages = (cur_heap_top + PGSIZE - 1) / PGSIZE;
    uint64 new_pages = (new_top + PGSIZE - 1) / PGSIZE;

    // 释放 [new_pages, cur_pages) 的页面
    for (uint64 p = new_pages ; p < cur_pages; p++) {
        uint64 va = p * PGSIZE;
        vm_unmappages(pgtbl, va, PGSIZE, true);
    }

    return new_top;
}

// 处理函数栈增长导致的page fault事件
// 成功返回new_ustack_npage，失败返回-1
uint64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_ustack_npage, uint64 fault_addr)
{
    // 检查page fault的地址是否合法
    if (fault_addr >= TRAPFRAME || fault_addr <= MMAP_END) return (uint64)-1; 

    // 取页对齐的 fault page VA
    uint64 fault_page_va = (fault_addr / PGSIZE) * PGSIZE;
    // 计算需要的栈页数
    uint64 need_pages = (TRAPFRAME - fault_page_va) / PGSIZE;

    // 如果已经包含该页，则无需扩展
    if (need_pages <= old_ustack_npage) {
        return old_ustack_npage;
    }

    // 边界检查：栈不能越过 MMAP_END
    uint64 max_stack_pages = (TRAPFRAME - (uint64)MMAP_END) / PGSIZE;
    if (need_pages > max_stack_pages) {
        return (uint64)-1;
    }

    // 为每一页分配物理页并映射
    for (uint64 i = old_ustack_npage+1 ; i <= need_pages; i++) {
        uint64 va = TRAPFRAME - i* PGSIZE; // 第 i 个栈页的虚拟地址
        void *pa = pmem_alloc(false);
        if (!pa) {
            uvm_unmap_existing_leaf_pages(pgtbl, TRAPFRAME - i * PGSIZE,
                                          TRAPFRAME - old_ustack_npage * PGSIZE);
            return (uint64)-1;
        }
        memset(pa, 0, PGSIZE);
        if (vm_try_mappages(pgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W | PTE_U) < 0) {
            pmem_free((uint64)pa, false);
            uvm_unmap_existing_leaf_pages(pgtbl, TRAPFRAME - i * PGSIZE,
                                          TRAPFRAME - old_ustack_npage * PGSIZE);
            return (uint64)-1;
        }
    }

    proc_t *p = myproc();
    p->ustack_npage = need_pages;

    return need_pages;
}

/*----------------------part-4: 用户页表管理相关----------------------*/

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3
extern char trampoline[];
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    if (pgtbl == NULL) return;

    int entries = PGSIZE / sizeof(pte_t); // 每页页表包含的条目数
    for (int i = 0; i < entries; i++) {
        pte_t pte = pgtbl[i];
        if (!(pte & PTE_V)) continue; // 无效条目跳过

        uint64 pa = (uint64)PTE_TO_PA(pte);
        int flags = PTE_FLAGS(pte);

        // 如果这是叶子（具有 R/W/X 权限）（普通页面或大页），释放对应的物理页
        if (level == 1 || (flags & (PTE_R | PTE_W | PTE_X))) {
            if (pa != (uint64)trampoline && !(flags & PTE_SHM)) { // TRAMPOLINE页面是全局共享的，不能释放
                pmem_free(pa, false);
            }
        } else {
            // 非叶子：递归销毁下一层页表
            destroy_pgtbl((pgtbl_t)pa, level - 1);
        }
    }
    pmem_free((uint64)pgtbl, true); // 释放当前页表页（在内核区域）
}

// 页表销毁
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    if (pgtbl == NULL)
        return;
    pte_t *tf_pte = vm_getpte(pgtbl, TRAPFRAME, false);
    if (tf_pte != NULL && (*tf_pte & PTE_V) && !PTE_CHECK(*tf_pte))
        vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, true);   // 可以释放，因为trapframe是每个进程独有的
    pte_t *tramp_pte = vm_getpte(pgtbl, TRAMPOLINE, false);
    if (tramp_pte != NULL && (*tramp_pte & PTE_V) && !PTE_CHECK(*tramp_pte))
        vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false); // 不能释放，因为所有进程共用区域
    destroy_pgtbl(pgtbl, 3);
}

static void destroy_shared_pgtbl_walk(pgtbl_t pgtbl, uint32 level)
{
    if (pgtbl == NULL) return;

    int entries = PGSIZE / sizeof(pte_t);
    for (int i = 0; i < entries; i++) {
        pte_t pte = pgtbl[i];
        if (!(pte & PTE_V)) continue;

        int flags = PTE_FLAGS(pte);
        if (level == 1 || (flags & (PTE_R | PTE_W | PTE_X)))
            continue;

        destroy_shared_pgtbl_walk((pgtbl_t)PTE_TO_PA(pte), level - 1);
    }
    pmem_free((uint64)pgtbl, true);
}

void uvm_destroy_shared_pgtbl(pgtbl_t pgtbl)
{
    if (pgtbl == NULL)
        return;
    pte_t *tf_pte = vm_getpte(pgtbl, TRAPFRAME, false);
    if (tf_pte != NULL && (*tf_pte & PTE_V) && !PTE_CHECK(*tf_pte))
        vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, true);
    pte_t *tramp_pte = vm_getpte(pgtbl, TRAMPOLINE, false);
    if (tramp_pte != NULL && (*tramp_pte & PTE_V) && !PTE_CHECK(*tramp_pte))
        vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false);
    destroy_shared_pgtbl_walk(pgtbl, 3);
}

// 连续虚拟空间的复制
// 在uvm_copy_pgtbl中使用
static int copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t *pte;

    for (va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");

        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        if (flags & PTE_SHM) {
            if (vm_try_mappages(new, va, pa, PGSIZE, flags) < 0)
                return -1;
        } else {
            page = (uint64)pmem_alloc(false);
            if (page == 0)
                return -1;
            memmove((char *)page, (const char *)pa, PGSIZE);
            if (vm_try_mappages(new, va, page, PGSIZE, flags) < 0) {
                pmem_free(page, false);
                return -1;
            }
        }
    }
    return 0;
}

static int copy_range_sparse_walk(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end, int level, uint64 base);

// 稀疏复制：跳过未映射的页
static int copy_range_sparse(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    if (begin >= end)
        return 0;
    if (begin % PGSIZE != 0 || end % PGSIZE != 0 || end > VA_MAX || end < begin)
        return -1;

    return copy_range_sparse_walk(old, new, begin, end, 2, 0);
}

static int copy_range_sparse_walk(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end, int level, uint64 base)
{
    if (old == NULL)
        return 0;

    uint64 span = 1UL << VA_SHIFT(level);
    int entries = PGSIZE / sizeof(pte_t);

    for (int i = 0; i < entries; i++) {
        uint64 entry_begin = base + (uint64)i * span;
        if (entry_begin >= end)
            break;
        uint64 entry_end = entry_begin + span;
        if (entry_end <= begin)
            continue;

        pte_t pte = old[i];
        if (!(pte & PTE_V))
            continue;

        if (level == 0 || !PTE_CHECK(pte)) {
            uint64 pa = (uint64)PTE_TO_PA(pte);
            int flags = (int)PTE_FLAGS(pte);
            uint64 va = entry_begin > begin ? entry_begin : begin;
            uint64 va_end = entry_end < end ? entry_end : end;

            for (; va < va_end; va += PGSIZE) {
                if (va == SIGTRAMPOLINE)
                    continue;
                if (flags & PTE_SHM) {
                    if (vm_try_mappages(new, va, pa + (va - entry_begin), PGSIZE, flags) < 0)
                        return -1;
                } else {
                    uint64 page = (uint64)pmem_alloc(false);
                    if (page == 0)
                        return -1;
                    memmove((char *)page, (const char *)(pa + (va - entry_begin)), PGSIZE);
                    if (vm_try_mappages(new, va, page, PGSIZE, flags) < 0) {
                        pmem_free(page, false);
                        return -1;
                    }
                }
            }
            continue;
        }

        if (copy_range_sparse_walk((pgtbl_t)PTE_TO_PA(pte), new, begin, end, level - 1, entry_begin) < 0)
            return -1;
    }
    return 0;
}

static int share_range_sparse_walk(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end, int level, uint64 base);

static int share_range_sparse(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    if (begin >= end)
        return 0;
    if (begin % PGSIZE != 0 || end % PGSIZE != 0 || end > VA_MAX || end < begin)
        return -1;

    return share_range_sparse_walk(old, new, begin, end, 2, 0);
}

static int share_range_sparse_walk(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end, int level, uint64 base)
{
    if (old == NULL)
        return 0;

    uint64 span = 1UL << VA_SHIFT(level);
    int entries = PGSIZE / sizeof(pte_t);

    for (int i = 0; i < entries; i++) {
        uint64 entry_begin = base + (uint64)i * span;
        if (entry_begin >= end)
            break;
        uint64 entry_end = entry_begin + span;
        if (entry_end <= begin)
            continue;

        pte_t pte = old[i];
        if (!(pte & PTE_V))
            continue;

        if (level == 0 || !PTE_CHECK(pte)) {
            uint64 pa = (uint64)PTE_TO_PA(pte);
            int flags = (int)PTE_FLAGS(pte);
            uint64 va = entry_begin > begin ? entry_begin : begin;
            uint64 va_end = entry_end < end ? entry_end : end;

            for (; va < va_end; va += PGSIZE) {
                if (va == SIGTRAMPOLINE)
                    continue;
                if (vm_try_mappages(new, va, pa + (va - entry_begin), PGSIZE, flags) < 0)
                    return -1;
            }
            continue;
        }

        if (share_range_sparse_walk((pgtbl_t)PTE_TO_PA(pte), new, begin, end, level - 1, entry_begin) < 0)
            return -1;
    }
    return 0;
}

// 拷贝页表 (拷贝并不包括 trapframe 和 trampoline)
// 拷贝的页表管理的物理页是原来页表的复制品
int uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint64 ustack_npage, mmap_region_t *mmap)
{
    // 复制用户 [PGSIZE, heap_top) 区域（code/data/heap）
    if (heap_top > PGSIZE) {
        uint64 begin = PGSIZE;
        uint64 end = ((heap_top + PGSIZE - 1) / PGSIZE) * PGSIZE; // 向上取整到页边界
        if (end > begin) {
            if (copy_range(old, new, begin, end) < 0)
                return -1;
        }
    }

    // 复制动态链接器区域 [heap_top, MMAP_BEGIN) — 稀疏复制跳过未映射页
    uint64 interp_begin = ((heap_top + PGSIZE - 1) / PGSIZE) * PGSIZE;
    if (interp_begin < MMAP_BEGIN) {
        if (copy_range_sparse(old, new, interp_begin, MMAP_BEGIN) < 0)
            return -1;
    }

    // 复制 mmap 链表所描述的离散映射区域
    mmap_region_t *m = mmap;
    while (m != NULL) {
        uint64 begin = m->begin;
        uint64 end = m->begin + (uint64)m->npages * PGSIZE;
        if (end > begin) {
            if (copy_range_sparse(old, new, begin, end) < 0)
                return -1;
        }
        m = m->next;
    }

    // 复制用户栈区域（从 TRAPFRAME - ustack_npage*PGSIZE 到 TRAPFRAME）
    if (ustack_npage > 0) {
        uint64 begin = TRAPFRAME - ustack_npage * PGSIZE;
        uint64 end = TRAPFRAME;
        if (end > begin) {
            if (copy_range(old, new, begin, end) < 0)
                return -1;
        }
    }
    return 0;
}

int uvm_share_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint64 ustack_npage, mmap_region_t *mmap)
{
    if (heap_top > PGSIZE) {
        uint64 begin = PGSIZE;
        uint64 end = ((heap_top + PGSIZE - 1) / PGSIZE) * PGSIZE;
        if (end > begin && share_range_sparse(old, new, begin, end) < 0)
            return -1;
    }

    uint64 interp_begin = ((heap_top + PGSIZE - 1) / PGSIZE) * PGSIZE;
    if (interp_begin < MMAP_BEGIN && share_range_sparse(old, new, interp_begin, MMAP_BEGIN) < 0)
        return -1;

    mmap_region_t *m = mmap;
    while (m != NULL) {
        uint64 begin = m->begin;
        uint64 end = m->begin + (uint64)m->npages * PGSIZE;
        if (end > begin && share_range_sparse(old, new, begin, end) < 0)
            return -1;
        m = m->next;
    }

    if (ustack_npage > 0) {
        uint64 begin = TRAPFRAME - ustack_npage * PGSIZE;
        if (TRAPFRAME > begin && share_range_sparse(old, new, begin, TRAPFRAME) < 0)
            return -1;
    }
    return 0;
}
