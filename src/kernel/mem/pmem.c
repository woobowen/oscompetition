#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
static alloc_region_t kern_region, user_region;

typedef struct pmem_trace {
    uint64 page;
    uint64 ra;
    uint8 in_kernel;
} pmem_trace_t;

#define PMEM_TRACE_N 256
static pmem_trace_t pmem_trace[PMEM_TRACE_N];
static uint32 pmem_trace_idx;

static void pmem_trace_record(uint64 page, bool in_kernel)
{
    uint32 idx = __sync_fetch_and_add(&pmem_trace_idx, 1) % PMEM_TRACE_N;
    pmem_trace[idx].page = page;
    pmem_trace[idx].ra = (uint64)__builtin_return_address(0);
    pmem_trace[idx].in_kernel = (uint8)(in_kernel ? 1 : 0);
}

static void pmem_trace_print_last_free(uint64 page)
{
    for (int i = 0; i < PMEM_TRACE_N; i++) {
        int idx = (int)((pmem_trace_idx + PMEM_TRACE_N - 1 - (uint32)i) % PMEM_TRACE_N);
        if (pmem_trace[idx].page == page) {
            printf("pmem_trace: last free page=%p ra=%p in_kernel=%d\n",
                   page, pmem_trace[idx].ra, pmem_trace[idx].in_kernel);
            return;
        }
    }
    printf("pmem_trace: no recent free record for page=%p\n", page);
}

// 物理内存的初始化
// 本质上就是填写kern_region和user_region, 包括基本数值和空闲链表
void pmem_init(void)
{
    // 初始化kern_region和user_region的锁
    spinlock_init(&kern_region.lk, "kernel_region_lock");
    spinlock_init(&user_region.lk, "user_region_lock");

    // 划分内核和用户内存区域的边界
    uint64 boundary = (uint64)ALLOC_BEGIN + KERN_PAGES * PGSIZE;

    // 初始化kern_region剩余内容
    kern_region.begin = (uint64)ALLOC_BEGIN;
    kern_region.end = boundary;
    kern_region.allocable = 0; // 可分配的空闲页面数
    kern_region.list_head.next = NULL; // 可分配链的链头节点

    // 初始化user_region剩余内容
    user_region.begin = boundary;
    user_region.end = (uint64)ALLOC_END;
    user_region.allocable = 0; 
    user_region.list_head.next = NULL; 

    // 将内核区域的物理页加入空闲链表
    // 相当于把内核区域的物理页都free掉
    for (uint64 p = kern_region.begin; p < kern_region.end; p += PGSIZE)
    {
        pmem_free(p, true);
    }
    // 将用户区域的物理页加入空闲链表
    for (uint64 p = user_region.begin; p < user_region.end; p += PGSIZE)
    {
        pmem_free(p, false);
    }
}

// 尝试返回一个可分配的清零后的物理页
// 失败则panic锁死
void* pmem_alloc(bool in_kernel)
{
    // 申请一个空闲页面：
    // 取出空闲链表的第一个空闲页作为分配的页面，并清零后返回

    page_node_t *page;

    // 分配区域：内核区域 or 用户区域
    alloc_region_t *ar = &user_region;
    if (in_kernel)
    {
        ar = &kern_region;
    }

    // 取出空闲链表的第一个节点
    spinlock_acquire(&ar->lk);
    page = ar->list_head.next;
    if (page) {
        // free list 健康性检查：避免 page->next 解引用触发内核 load page fault
        uint64 pa = (uint64)page;
        if (pa % PGSIZE != 0 || pa < ar->begin || pa >= ar->end) {
            spinlock_release(&ar->lk);
            printf("pmem_alloc: free list head corrupted: page=%p region=[%p,%p) in_kernel=%d ra=%p\n",
                   page, ar->begin, ar->end, in_kernel, __builtin_return_address(0));
            pmem_trace_print_last_free(pa);
            panic("pmem_alloc: free list corrupted");
        }

        // 进一步检查 next 指针：page 合法但其头部(next)可能被写坏
        page_node_t *next = page->next;
        if (next != NULL) {
            uint64 npa = (uint64)next;
            if (npa % PGSIZE != 0 || npa < ar->begin || npa >= ar->end) {
                spinlock_release(&ar->lk);
                printf("pmem_alloc: free list next corrupted: page=%p next=%p region=[%p,%p) in_kernel=%d\n",
                       page, next, ar->begin, ar->end, in_kernel);
                pmem_trace_print_last_free(pa);
                panic("pmem_alloc: free list corrupted (next)");
            }
        }

        ar->list_head.next = page->next;
        ar->allocable--;
    }
    spinlock_release(&ar->lk);

    // 分配失败，则panic锁死
    if (!page)
        return NULL;

    // 清零后返回
    memset(page, 0, PGSIZE);
    return page;
}

// 释放一个物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{
    // page: 要释放的物理页的起始地址
    // 释放一个之前申请的物理页：
    // 将它插入空闲链表的表头

    // 分配区域：内核区域 or 用户区域
    alloc_region_t *ar = &user_region;
    if (in_kernel)
    {
        ar = &kern_region;
    }

    // 检查page的合法性
    // 注意：ar->end 是开区间端点，合法范围为 [begin, end)
    if (page % PGSIZE != 0 || page < ar->begin || page >= ar->end)
    {
        panic("pmem_free: invalid page");
    }

    // 调试用：填充为垃圾值
    memset((void *)page, 1, PGSIZE);

    // 插入到空闲链表的表头
    page_node_t *p = (page_node_t *)page;
    spinlock_acquire(&ar->lk);
    p->next = ar->list_head.next;
    ar->list_head.next = p;
    ar->allocable++;
    spinlock_release(&ar->lk);

    pmem_trace_record(page, in_kernel);
}

// 获取可用内存信息
void pmem_stat(uint32 *free_pages_in_kernel, uint32 *free_pages_in_user)
{
    // 获取内核区域的空闲页面数
    spinlock_acquire(&kern_region.lk);
    *free_pages_in_kernel = kern_region.allocable;
    spinlock_release(&kern_region.lk);

    // 获取用户区域的空闲页面数
    spinlock_acquire(&user_region.lk);
    *free_pages_in_user = user_region.allocable;
    spinlock_release(&user_region.lk);
}
