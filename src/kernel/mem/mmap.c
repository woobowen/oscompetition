#include "mod.h"

// mmap_region_node_t 仓库：固定大小的节点仓库（N_MMAP个节点，所有进程来仓库借/还一个节点）
// mmap_region_node_t 仓库(单向链表) + 链表头节点(不可分配) + 保护仓库的自旋锁
static mmap_region_node_t node_list[N_MMAP];
static mmap_region_node_t list_head;
static spinlock_t list_lk;

// 初始化上述三个数据结构
// 初始化仓库
void mmap_init()
{
    spinlock_init(&list_lk, "mmap_region_node_list_lock");

    // 将所有节点连成单链表
    list_head.next = NULL;
    for (int i = 0; i < N_MMAP; i++)
    {
        node_list[i].next = (i + 1 < N_MMAP) ? &node_list[i + 1] : NULL;
        node_list[i].mmap.begin = 0;
        node_list[i].mmap.npages = 0;
        node_list[i].mmap.perm = 0;
        node_list[i].mmap.next = NULL;
    }
    list_head.next = &node_list[0];
}

// 从仓库申请一个 mmap_region_t
// 若仓库空了则 panic
mmap_region_t *mmap_region_alloc()
{
    spinlock_acquire(&list_lk);

    // 每次取仓库链表的第一个节点
    mmap_region_node_t *node = list_head.next;
    if (node == NULL)
    {
        spinlock_release(&list_lk);
        panic("mmap_region_alloc: no more mmap_region_node available");
    }
    list_head.next = node->next; 

    // 将取出的node与仓库断开连接 + 清理内部链表指针
    node->next = NULL;
    node->mmap.begin = 0;
    node->mmap.npages = 0;
    node->mmap.perm = 0;
    node->mmap.next = NULL;

    spinlock_release(&list_lk);

    return &node->mmap;
}

// 向仓库归还一个 mmap_region_t
void mmap_region_free(mmap_region_t *mmap)
{
    if (mmap == NULL)
        return;

    // 找到 mmap 所在的 node
    // 因为 mmap 是 mmap_region_node_t 结构体的第一个成员，所以它们地址相同
    mmap_region_node_t *node = (mmap_region_node_t *)mmap;

    spinlock_acquire(&list_lk);

    // 头插法归还节点
    node->next = list_head.next;
    list_head.next = node;

    spinlock_release(&list_lk);
}

// 输出可用的 mmap_region_node_t 链
// for debug
void mmap_show_nodelist()
{
    spinlock_acquire(&list_lk);

    mmap_region_node_t *tmp = list_head.next;
    int node = 0, index = 0;
    while (tmp)
    {
        index = tmp - &(node_list[0]);
        printf("node %d index = %d\n", node++, index);
        tmp = tmp->next;
    }

    spinlock_release(&list_lk);
}
