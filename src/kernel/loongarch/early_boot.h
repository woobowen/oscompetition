#ifndef SEAOS_LOONGARCH_EARLY_BOOT_H
#define SEAOS_LOONGARCH_EARLY_BOOT_H

#include <stdint.h>
#include "trap_layout.h"

/* ---- Kernel entry address ---- */
#define LA_KERNEL_ENTRY 0x200000ULL
#define LA_TEXT_OFFSET  0x1000ULL

/* ---- Stub boot log (used by la_elfgen only) ---- */
#define LA_EARLY_BOOT_LOG \
    "loongarch boot start\n" \
    "la_entry: qemu virt early console online\n" \
    "la_boot_main: arch scaffold active\n" \
    "la_trap: full trapframe syscall return scaffold staged\n" \
    "la_userret: era prmd gpr restore scaffold staged\n" \
    "la_proc: trapframe init userret bridge staged\n" \
    "la_boot_main: next context switch virtio-pci ext4\n"

/* ---- UART (LoongArch QEMU virt 8250) ---- */
#define LA_UART_BASE 0x1fe001e0U

/* ---- Page size ---- */
#define LA_PGSIZE   4096U
#define LA_PGSHIFT  12

/* ---- User address space layout ---- */
#define LA_USER_BASE    0x1000ULL     /* user code starts here */
#define LA_USER_STACK   0x7FFFFFE000ULL  /* user stack top (39-bit space) */
#define LA_MMAP_BASE    0x4000000000ULL  /* mmap region start (256 GB, grows up;
                                           * disjoint from brk heap & stack) */

/* ---- Physical memory layout (QEMU virt, -m 1G) ----
 *
 *  Low  memory: 0x00000000 – 0x0FFFFFFF  (256 MB)
 *  Device  I/O: 0x10000000 – 0x8FFFFFFF
 *  High memory: 0x90000000 – 0xBFFFFFFF  (768 MB, remaining RAM)
 *
 *  Kernel is loaded at 0x200000 (inside low memory).
 *  We only manage low memory for now (~254 MB available).
 */
#define LA_LOWMEM_END   0x10000000ULL
#define LA_HIGHMEM_BASE 0x90000000ULL

/* ---- Stable Timer (cpucfg[4] reports 100 MHz in QEMU virt) ---- */
#define LA_TIMER_FREQ     100000000ULL    /* 100 MHz */
#define LA_TIMER_HZ       100U            /* 100 ticks / sec */
#define LA_TIMER_INTERVAL (LA_TIMER_FREQ / LA_TIMER_HZ)  /* 1 000 000 */

/* ---- Linker symbol (defined in kernel.ld) ---- */
extern char la_kernel_end[];

/* ---- CSR access macros ---- */
#define la_csr_read(csr) \
    ({ uint64_t __v; asm volatile("csrrd %0, %1" : "=r"(__v) : "i"(csr)); __v; })

#define la_csr_write(val, csr) \
    do { asm volatile("csrwr %0, %1" : : "r"((uint64_t)(val)), "i"(csr)); } while (0)

/* ---- Early UART ---- */
void la_uart_putc(char c);
void la_uart_puts(const char *s);
void la_uart_put_hex(uint64_t value);

/* ---- Subsystem init ---- */
void la_pmem_init(void);
void la_kvm_init(void);
void la_timer_init(void);

/* ---- Timer interrupt handler (called from trap_dispatch) ---- */
void la_timer_interrupt(void);
uint64_t la_timer_get_ticks(void);     /* monotonic tick counter (100 Hz) */

/* ---- Physical memory allocator ---- */
void *la_pmem_alloc(void);
void  la_pmem_free(void *page);

/* ---- VirtIO PCI block device ---- */
void la_virtio_init(void);
int  la_virtio_blk_read(uint32_t block_num, void *buf);
int  la_virtio_blk_write(uint32_t block_num, const void *buf);

/* ---- Filesystem (read-only, SeaOS FS + EXT4) ---- */
int     la_fs_init(void);
extern uint32_t la_fs_cwd_ino;   /* current proc cwd inode (set by syscall entry) */
int     la_fs_lookup(char *path, uint32_t *inode_num);
uint32_t la_fs_read_file(uint32_t inode_num, uint32_t offset,
                          void *dst, uint32_t len);
void    la_fs_list_dir(uint32_t dir_ino);
int     la_fs_inode_type(uint32_t ino);
uint32_t la_fs_inode_size(uint32_t ino);
uint32_t la_fs_get_dentries(uint32_t dir_ino, void *dst, uint32_t len);
int     la_fs_is_sea(void);

/* ---- Memory filesystem (writable) ---- */
void     memfs_init(void);
int      memfs_lookup(const char *path);
int      memfs_create(const char *path, int type);
int      memfs_write(int ino, uint32_t offset, const void *buf, uint32_t len);
int      memfs_read(int ino, uint32_t offset, void *buf, uint32_t len);
int      memfs_delete(const char *path);
int      memfs_getdents(int dir_ino, void *buf, uint32_t len);
uint32_t memfs_inode_size(int ino);
int      memfs_path_prefix(const char *path, const char *prefix);

/* ---- User virtual memory ---- */
uint64_t *la_uvm_create(void);
int      la_uvm_map_page(uint64_t *root, uint64_t va, uint64_t pa, uint64_t perm);
uint64_t la_uvm_alloc_page(uint64_t *root, uint64_t va, uint64_t perm);
/* Grow the current proc's user stack down to cover fault_addr.
 * Returns 0 on success (pages now mapped), -1 if fault_addr is outside the
 * allowed stack region [LA_USER_STACK - LA_MAX_STACK_PAGES*PGSIZE, stack_bottom). */
int      la_uvm_grow_stack(uint64_t *root, uint64_t fault_addr);
void     la_uvm_copy_in(uint64_t *root, uint64_t va, const void *src, uint32_t len);
void     la_uvm_paging_init(void);
void     la_uvm_switch(uint64_t *pgtbl);
int      la_uvm_copy_pgtbl(uint64_t *src, uint64_t *dst);
void     la_uvm_free_pgtbl(uint64_t *root);   /* free a whole user page table + all mapped data pages */
uint64_t la_uva_to_pa(uint64_t *root, uint64_t va);

/* ---- User ↔ Kernel data copy ---- */
uint32_t la_copy_from_user(void *kdst, uint64_t usrc, uint32_t len);
uint32_t la_copy_to_user(uint64_t udst, const void *ksrc, uint32_t len);
int      la_copy_str_from_user(char *kdst, uint64_t usrc, uint32_t max);

/* ---- TLB management ---- */
void la_tlb_init(void);
void la_tlb_inval_all(void);
void la_tlb_inval_all_deep(void);            /* deep (O(2112)) — for pgtbl free only */
void la_tlb_inval_page(uint64_t va);
int  la_tlb_fill_all(uint64_t *pgtbl);
int  la_tlb_refill_one(uint64_t va);
void la_tlb_dump(int max_entries);
extern uint64_t la_tlb_active_pgtbl;  /* set by proc.c before user mode */

/* ---- First user process ---- */
void la_proc_make_first(void);

#endif
