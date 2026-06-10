/*
 * LoongArch physical memory manager.
 *
 * Simple free-list page allocator for the low-memory region
 * (kernel_end … 0x10000000).  Each free page's first 8 bytes hold a
 * next-pointer; allocation pops from the head, free pushes to the head.
 */
#include "early_boot.h"

/* ---- internal free-list node (lives at the start of each free page) ---- */
struct la_page_node {
    struct la_page_node *next;
};

static struct la_page_node *la_free_list;
static uint64_t la_free_pages;
static uint64_t la_total_pages;

/* ---- init: populate free list from kernel_end to lowmem_end ---- */
void la_pmem_init(void)
{
    uint64_t start = (uint64_t)la_kernel_end;
    uint64_t end   = LA_LOWMEM_END;

    /* page-align boundaries */
    start = (start + LA_PGSIZE - 1) & ~((uint64_t)LA_PGSIZE - 1);
    end   = end & ~((uint64_t)LA_PGSIZE - 1);

    la_uart_puts("  pmem: region ");
    la_uart_put_hex(start);
    la_uart_puts(" - ");
    la_uart_put_hex(end);
    la_uart_puts("\n");

    la_free_list  = 0;
    la_free_pages = 0;
    la_total_pages = 0;

    for (uint64_t p = start; p + LA_PGSIZE <= end; p += LA_PGSIZE) {
        struct la_page_node *node = (struct la_page_node *)p;
        node->next = la_free_list;
        la_free_list = node;
        la_free_pages++;
        la_total_pages++;
    }

    la_uart_puts("  pmem: ");
    la_uart_put_hex(la_total_pages);
    la_uart_puts(" pages (");
    la_uart_put_hex(la_total_pages * LA_PGSIZE / (1024 * 1024));
    la_uart_puts(" MB)\n");
}

/* ---- allocate one zeroed page ---- */
void *la_pmem_alloc(void)
{
    if (!la_free_list)
        return 0;

    struct la_page_node *page = la_free_list;
    la_free_list = page->next;
    la_free_pages--;

    /* zero-fill */
    uint64_t *p = (uint64_t *)page;
    uint64_t *end = (uint64_t *)((char *)page + LA_PGSIZE);
    while (p < end)
        *p++ = 0;

    return (void *)page;
}

/* ---- return a page to the free list ---- */
void la_pmem_free(void *page)
{
    if (!page)
        return;

    uint64_t addr = (uint64_t)page;
    if (addr % LA_PGSIZE != 0)
        return;

    struct la_page_node *node = (struct la_page_node *)page;
    node->next = la_free_list;
    la_free_list = node;
    la_free_pages++;
}
