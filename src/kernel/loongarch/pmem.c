/*
 * LoongArch physical memory manager.
 *
 * Simple free-list page allocator for the low-memory region
 * (kernel_end .. 0x10000000).  Each free page's first 8 bytes hold a
 * next-pointer; allocation pops from the head, free pushes to the head.
 *
 * High-memory experiments showed that the current page-table and kernel-copy
 * paths still assume low physical addresses can be used as kernel pointers.
 * Keep production allocation low-only until the PA/KVA split is complete.
 */
#include "early_boot.h"

struct la_page_node {
    struct la_page_node *next;
};

static struct la_page_node *la_free_list;
static uint64_t la_free_pages;
static uint64_t la_total_pages;
static uint8_t la_page_used[LA_LOWMEM_END / LA_PGSIZE];
static uint16_t la_page_refcnt[LA_LOWMEM_END / LA_PGSIZE];

static int la_pmem_page_index(uint64_t addr, uint64_t *idx)
{
    addr = la_kva_to_pa(addr);
    if (addr % LA_PGSIZE != 0)
        return -1;
    if (addr < (uint64_t)la_kernel_end || addr >= LA_LOWMEM_END)
        return -1;
    *idx = addr / LA_PGSIZE;
    return 0;
}

static void la_pmem_zero_page(uint64_t pa)
{
    uint64_t *p = (uint64_t *)la_pa_to_kva(pa);
    uint64_t *end = (uint64_t *)(la_pa_to_kva(pa) + LA_PGSIZE);
    while (p < end)
        *p++ = 0;
}

void la_pmem_init(void)
{
    uint64_t start = (uint64_t)la_kernel_end;
    uint64_t end   = LA_LOWMEM_END;

    start = (start + LA_PGSIZE - 1) & ~((uint64_t)LA_PGSIZE - 1);
    end   = end & ~((uint64_t)LA_PGSIZE - 1);

    la_uart_puts("  pmem: region ");
    la_uart_put_hex(start);
    la_uart_puts(" - ");
    la_uart_put_hex(end);
    la_uart_puts("\n");

    la_free_list = 0;
    la_free_pages = 0;
    la_total_pages = 0;
    for (uint64_t i = 0; i < sizeof(la_page_used); i++)
        la_page_used[i] = 1;
    for (uint64_t i = 0; i < sizeof(la_page_refcnt) / sizeof(la_page_refcnt[0]); i++)
        la_page_refcnt[i] = 0;

    for (uint64_t p = start; p + LA_PGSIZE <= end; p += LA_PGSIZE) {
        struct la_page_node *node = (struct la_page_node *)p;
        node->next = la_free_list;
        la_free_list = node;
        la_page_used[p / LA_PGSIZE] = 0;
        la_page_refcnt[p / LA_PGSIZE] = 0;
        la_free_pages++;
        la_total_pages++;
    }

    la_uart_puts("  pmem: ");
    la_uart_put_hex(la_total_pages);
    la_uart_puts(" pages (");
    la_uart_put_hex(la_total_pages * LA_PGSIZE / (1024 * 1024));
    la_uart_puts(" MB)\n");
}

void *la_pmem_alloc(void)
{
    if (!la_free_list)
        return 0;

    struct la_page_node *page = la_free_list;
    la_free_list = page->next;
    la_free_pages--;
    uint64_t idx = (uint64_t)page / LA_PGSIZE;
    la_page_used[idx] = 1;
    la_page_refcnt[idx] = 1;

    la_pmem_zero_page((uint64_t)page);

    return (void *)page;
}

void *la_pmem_alloc_user_page(void)
{
    return la_pmem_alloc();
}

void la_pmem_ref_inc(void *page)
{
    if (!page)
        return;

    uint64_t addr = la_kva_to_pa((uint64_t)page);
    uint64_t idx = 0;
    if (la_pmem_page_index(addr, &idx) < 0) {
        la_uart_puts("  pmem: reject ref inc ");
        la_uart_put_hex(addr);
        la_uart_puts("\n");
        return;
    }
    if (!la_page_used[idx] || la_page_refcnt[idx] == 0) {
        la_uart_puts("  pmem: ref inc free page ");
        la_uart_put_hex(addr);
        la_uart_puts("\n");
        return;
    }
    if (la_page_refcnt[idx] == 0xffffU) {
        la_uart_puts("  pmem: refcount overflow ");
        la_uart_put_hex(addr);
        la_uart_puts("\n");
        return;
    }
    la_page_refcnt[idx]++;
}

void la_pmem_free(void *page)
{
    if (!page)
        return;

    uint64_t addr = la_kva_to_pa((uint64_t)page);
    uint64_t idx = 0;
    if (la_pmem_page_index(addr, &idx) < 0) {
        la_uart_puts("  pmem: reject unaligned free ");
        la_uart_put_hex(addr);
        la_uart_puts("\n");
        return;
    }
    if (!la_page_used[idx]) {
        la_uart_puts("  pmem: reject double free ");
        la_uart_put_hex(addr);
        la_uart_puts("\n");
        return;
    }
    if (la_page_refcnt[idx] > 1) {
        la_page_refcnt[idx]--;
        return;
    }
    la_page_refcnt[idx] = 0;
    la_page_used[idx] = 0;

    struct la_page_node *node = (struct la_page_node *)addr;
    node->next = la_free_list;
    la_free_list = node;
    la_free_pages++;
}
