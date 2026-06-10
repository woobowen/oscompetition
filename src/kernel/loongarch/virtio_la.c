/*
 * LoongArch VirtIO PCI block device driver.
 *
 * LoongArch QEMU virt uses virtio-blk-pci (PCI bus) unlike RISC-V's
 * virtio-blk-device (MMIO).  This driver:
 *
 *   1. Scans PCI bus 0 via ECAM (0x20000000) for a VirtIO block device
 *   2. Assigns BAR addresses and enables the device
 *   3. Inits the VirtIO device (same legacy protocol as MMIO)
 *   4. Provides polling-based block read/write (no interrupts yet)
 *
 * Key QEMU LoongArch virt addresses:
 *   PCI ECAM  : 0x20000000  (config space)
 *   PCI PIO   : 0x18004000  (I/O bar window)
 *   PCI MMIO  : 0x40000000  (memory bar window)
 */
#include "early_boot.h"

/* ================================================================
 *  PCI constants
 * ================================================================ */
#define LA_PCI_ECAM_BASE    0x20000000ULL
#define LA_PCI_MMIO_BASE    0x40000000ULL
#define LA_PCI_PIO_BASE     0x18004000ULL

#define PCI_CONFIG_COMMAND      0x04
#define PCI_CONFIG_BAR(n)       (0x10 + (n) * 4)
#define PCI_CMD_IO_ENABLE       (1 << 0)
#define PCI_CMD_MEM_ENABLE      (1 << 1)
#define PCI_CMD_BUS_MASTER      (1 << 2)

#define VIRTIO_PCI_VENDOR       0x1AF4

/* ================================================================
 *  VirtIO PCI legacy register offsets
 *  (different from MMIO!  No magic/version at offset 0.)
 * ================================================================ */
#define VIRTIO_PCI_HOST_FEATURES    0x00  /* u32 RO  device features */
#define VIRTIO_PCI_GUEST_FEATURES   0x04  /* u32 RW  driver features */
#define VIRTIO_PCI_QUEUE_PFN        0x08  /* u32 RW  queue phys page # */
#define VIRTIO_PCI_QUEUE_NUM        0x0C  /* u16 RO  max queue size   */
#define VIRTIO_PCI_QUEUE_SEL        0x0E  /* u16 RW  queue selector   */
#define VIRTIO_PCI_QUEUE_NOTIFY     0x10  /* u16 RW  kick queue       */
#define VIRTIO_PCI_STATUS           0x12  /* u8  RW  device status    */
#define VIRTIO_PCI_ISR              0x13  /* u8  RO  interrupt status */

#define VIRTIO_CONFIG_S_ACKNOWLEDGE     1
#define VIRTIO_CONFIG_S_DRIVER          2
#define VIRTIO_CONFIG_S_DRIVER_OK       4
#define VIRTIO_CONFIG_S_FEATURES_OK     8

#define VIRTIO_BLK_F_RO         5
#define VIRTIO_BLK_F_SCSI       7
#define VIRTIO_BLK_F_CONFIG_WCE 11
#define VIRTIO_BLK_F_MQ         12
#define VIRTIO_F_ANY_LAYOUT     27
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX 29

#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1

#define VRING_DESC_F_NEXT       1
#define VRING_DESC_F_WRITE      2

#define VIRTIO_NUM              8
#define VIRTIO_RING_SIZE        256   /* max entries device supports */
#define LA_BLOCK_SIZE           4096

/* ================================================================
 *  VirtQueue data structures
 * ================================================================ */
typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} la_vring_desc_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} la_vring_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t id;
    la_vring_used_elem_t elems[VIRTIO_RING_SIZE];
} la_used_area_t;

/* Block request header (must match QEMU virtio-blk.c) */
struct la_virtio_blk_outhdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

/* ================================================================
 *  Driver state
 * ================================================================ */

/* 3 pages for 256-entry virtqueue:
 *   page 0: descriptors (256 × 16 = 4096)
 *   page 1: available ring (518 bytes)
 *   page 2: used ring (2054 bytes)
 */
static __attribute__((aligned(4096))) char la_vq_pages[3 * 4096];

static la_vring_desc_t *la_desc;
static uint16_t        *la_avail;
static la_used_area_t  *la_used;
static char             la_desc_free[VIRTIO_NUM];
static uint16_t         la_used_idx;

/* Device register base (found during PCI init) */
static uint64_t la_virtio_base;

static inline volatile uint32_t *la_vreg(uint32_t offset)
{
    return (volatile uint32_t *)(la_virtio_base + offset);
}

/* ================================================================
 *  PCI config-space access (ECAM)
 * ================================================================ */

static uint32_t la_pci_read(int bus, int dev, int func, int reg)
{
    volatile uint32_t *p = (volatile uint32_t *)
        (LA_PCI_ECAM_BASE
         + ((uint64_t)bus  << 20)
         + ((uint64_t)dev  << 15)
         + ((uint64_t)func << 12)
         + (reg & ~3));
    return *p;
}

static void la_pci_write(int bus, int dev, int func, int reg, uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)
        (LA_PCI_ECAM_BASE
         + ((uint64_t)bus  << 20)
         + ((uint64_t)dev  << 15)
         + ((uint64_t)func << 12)
         + (reg & ~3));
    *p = val;
}

/* ================================================================
 *  Descriptor helpers
 * ================================================================ */

static int la_alloc_desc(void)
{
    for (int i = 0; i < VIRTIO_NUM; i++) {
        if (la_desc_free[i]) {
            la_desc_free[i] = 0;
            return i;
        }
    }
    return -1;
}

static void la_free_desc(int i)
{
    la_desc[i].addr = 0;
    la_desc_free[i] = 1;
}

static void la_free_chain(int i)
{
    while (1) {
        int next = -1;
        if (la_desc[i].flags & VRING_DESC_F_NEXT)
            next = la_desc[i].next;
        la_free_desc(i);
        if (next >= 0)
            i = next;
        else
            break;
    }
}

static int la_alloc3_desc(int idx[3])
{
    for (int i = 0; i < 3; i++) {
        idx[i] = la_alloc_desc();
        if (idx[i] < 0) {
            for (int j = 0; j < i; j++)
                la_free_desc(idx[j]);
            return -1;
        }
    }
    return 0;
}

/* ================================================================
 *  Public API: init / read / write
 * ================================================================ */

void la_virtio_init(void)
{
    /* ---- 1. Scan PCI bus 0 for VirtIO BLOCK device ---- */
    int found_dev = -1;

    for (int d = 0; d < 32; d++) {
        uint32_t id = la_pci_read(0, d, 0, 0x00);
        uint16_t vendor = id & 0xFFFF;
        uint16_t device = (id >> 16) & 0xFFFF;
        if (vendor == 0xFFFF)
            continue;  /* empty slot */

        la_uart_puts("    pci ");
        la_uart_put_hex(d);
        la_uart_puts(": vendor=");
        la_uart_put_hex(vendor);
        la_uart_puts(" device=");
        la_uart_put_hex(device);
        la_uart_puts("\n");

        /* VirtIO block:
         *   0x1001 = VirtIO transitional block
         *   0x1042 = VirtIO non-transitional block
         * Note: 0x1000 is the VirtIO network card — do NOT match it. */
        if (vendor == VIRTIO_PCI_VENDOR
            && (device == 0x1001 || device == 0x1042)) {
            found_dev = d;
            /* don't break — keep scanning to show all devices */
        }
    }

    if (found_dev < 0) {
        la_uart_puts("  virtio: no VirtIO block device on PCI bus\n");
        return;
    }

    /* ---- 2. Enable device fully first ---- */
    uint32_t cmd = la_pci_read(0, found_dev, 0, PCI_CONFIG_COMMAND);
    la_uart_puts("    initial CMD=");
    la_uart_put_hex(cmd);
    la_uart_puts("\n");
    cmd |= PCI_CMD_IO_ENABLE | PCI_CMD_MEM_ENABLE | PCI_CMD_BUS_MASTER;
    la_pci_write(0, found_dev, 0, PCI_CONFIG_COMMAND, cmd);
    cmd = la_pci_read(0, found_dev, 0, PCI_CONFIG_COMMAND);
    la_uart_puts("    CMD after write=");
    la_uart_put_hex(cmd);
    la_uart_puts("\n");

    /* ---- 3. Assign BARs with bump allocator ----
     *
     * Key insight from QEMU source (hw/loongarch/virt.c):
     *   MMIO alias:  memory_region_init_alias(..., mmio_reg, mmio_base, mmio_size)
     *     → CPU phys [0x40000000, 0x80000000) maps to PCI mem [0x40000000, 0x80000000)
     *     → BAR address must be in 0x40000000…0x7FFFFFFF
     *
     *   PIO alias:   memory_region_init_alias(..., pio_reg, 0x4000, pio_size)
     *     → CPU phys [0x18004000, 0x18010000) maps to PCI I/O [0x4000, 0x10000)
     *     → BAR I/O address must be in 0x4000…0xFFFF
     */
    la_virtio_base = 0;
    uint64_t next_mmio = LA_PCI_MMIO_BASE;      /* 0x40000000 */
    uint64_t next_pio  = 0x4000ULL;              /* PIO start offset */

    int bar_idx = 0;
    while (bar_idx < 6) {
        int reg = PCI_CONFIG_BAR(bar_idx);

        /* Probe BAR size */
        la_pci_write(0, found_dev, 0, reg, 0xFFFFFFFF);
        uint32_t probe = la_pci_read(0, found_dev, 0, reg);
        la_pci_write(0, found_dev, 0, reg, 0x00000000);

        if (probe == 0 || probe == 0xFFFFFFFF) {
            bar_idx++;
            continue;
        }

        int is_io = probe & 1;

        if (is_io) {
            /* I/O BAR — address must be in [0x4000, 0xFFFF] */
            uint32_t size = ~(probe & ~0x3U) + 1;
            uint32_t pio_addr = (uint32_t)next_pio;
            next_pio += size;

            la_pci_write(0, found_dev, 0, reg, pio_addr | 1);
            uint32_t rb = la_pci_read(0, found_dev, 0, reg);

            /* CPU addr = LA_PCI_PIO_BASE + (pio_addr - 0x4000) */
            uint64_t cpu_addr = LA_PCI_PIO_BASE + (pio_addr - 0x4000);

            la_uart_puts("    BAR");
            la_uart_put_hex(bar_idx);
            la_uart_puts(" IO sz=");
            la_uart_put_hex(size);
            la_uart_puts(" pio=");
            la_uart_put_hex(pio_addr);
            la_uart_puts(" rb=");
            la_uart_put_hex(rb);
            la_uart_puts(" cpu=");
            la_uart_put_hex(cpu_addr);
            la_uart_puts("\n");

            uint32_t val = *(volatile uint32_t *)cpu_addr;
            la_uart_puts("      io_val=");
            la_uart_put_hex(val);
            la_uart_puts("\n");

            /* PCI legacy: no magic — use I/O BAR directly */
            la_virtio_base = cpu_addr;
        } else {
            /* Memory BAR — address must be in [0x40000000, 0x7FFFFFFF] */
            int is_64 = ((probe >> 1) & 3) == 2;
            uint32_t size = ~(probe & ~0xFU) + 1;
            uint64_t mem_addr = next_mmio;
            /* Align to size */
            mem_addr = (mem_addr + size - 1) & ~((uint64_t)size - 1);
            next_mmio = mem_addr + size;

            la_pci_write(0, found_dev, 0, reg, (uint32_t)mem_addr);
            if (is_64) {
                la_pci_write(0, found_dev, 0, reg + 4, (uint32_t)(mem_addr >> 32));
            }
            uint32_t rb = la_pci_read(0, found_dev, 0, reg);

            /* CPU addr == PCI mem addr (identity-mapped alias) */
            uint64_t cpu_addr = mem_addr;

            la_uart_puts("    BAR");
            la_uart_put_hex(bar_idx);
            la_uart_puts(" MEM");
            if (is_64) la_uart_puts("(64)");
            la_uart_puts(" sz=");
            la_uart_put_hex(size);
            la_uart_puts(" pci=");
            la_uart_put_hex((uint32_t)mem_addr);
            la_uart_puts(" rb=");
            la_uart_put_hex(rb);
            la_uart_puts(" cpu=");
            la_uart_put_hex(cpu_addr);
            la_uart_puts("\n");

            if (is_64)
                bar_idx++;  /* skip upper 32 bits */
        }

        if (la_virtio_base)
            break;

        bar_idx++;
    }

    if (!la_virtio_base) {
        la_uart_puts("  virtio: could not locate VirtIO registers\n");
        return;
    }

    /* ---- 4. VirtIO PCI legacy device init ---- */

    /* Register access helpers (I/O BAR, various widths) */
    #define VR32(off) (*(volatile uint32_t *)(la_virtio_base + (off)))
    #define VR16(off) (*(volatile uint16_t *)(la_virtio_base + (off)))
    #define VR8(off)  (*(volatile uint8_t  *)(la_virtio_base + (off)))

    /* Step 1: Acknowledge */
    VR8(VIRTIO_PCI_STATUS) = VIRTIO_CONFIG_S_ACKNOWLEDGE;

    /* Step 2: Driver */
    VR8(VIRTIO_PCI_STATUS) = VIRTIO_CONFIG_S_ACKNOWLEDGE
                           | VIRTIO_CONFIG_S_DRIVER;

    /* Step 3: Negotiate features */
    uint32_t host_feat = VR32(VIRTIO_PCI_HOST_FEATURES);
    la_uart_puts("    host_features=");
    la_uart_put_hex(host_feat);
    la_uart_puts("\n");

    uint32_t feat = host_feat;
    feat &= ~((uint32_t)1 << VIRTIO_BLK_F_RO);
    feat &= ~((uint32_t)1 << VIRTIO_BLK_F_SCSI);
    feat &= ~((uint32_t)1 << VIRTIO_BLK_F_CONFIG_WCE);
    feat &= ~((uint32_t)1 << VIRTIO_BLK_F_MQ);
    feat &= ~((uint32_t)1 << VIRTIO_F_ANY_LAYOUT);
    feat &= ~((uint32_t)1 << VIRTIO_RING_F_EVENT_IDX);
    feat &= ~((uint32_t)1 << VIRTIO_RING_F_INDIRECT_DESC);
    VR32(VIRTIO_PCI_GUEST_FEATURES) = feat;

    /* Step 4: Features OK */
    VR8(VIRTIO_PCI_STATUS) = VIRTIO_CONFIG_S_ACKNOWLEDGE
                           | VIRTIO_CONFIG_S_DRIVER
                           | VIRTIO_CONFIG_S_FEATURES_OK;

    /* Step 5: Setup virtqueue 0 (BEFORE DRIVER_OK!) */
    VR16(VIRTIO_PCI_QUEUE_SEL) = 0;
    uint32_t qmax = VR16(VIRTIO_PCI_QUEUE_NUM);
    la_uart_puts("    queue_max=");
    la_uart_put_hex(qmax);
    la_uart_puts("\n");

    if (qmax == 0) {
        la_uart_puts("  virtio: queue 0 missing\n");
        return;
    }

    /* Zero queue pages */
    for (int i = 0; i < (int)(3 * 4096); i++)
        la_vq_pages[i] = 0;

    /* Write PFN (physical page number) of virtqueue */
    uint32_t pfn = ((uint32_t)(uint64_t)la_vq_pages) >> 12;
    la_uart_puts("    vq_pages=");
    la_uart_put_hex((uint64_t)la_vq_pages);
    la_uart_puts(" pfn=");
    la_uart_put_hex(pfn);
    la_uart_puts("\n");
    VR32(VIRTIO_PCI_QUEUE_PFN) = pfn;

    la_desc  = (la_vring_desc_t *)la_vq_pages;
    la_avail = (uint16_t *)(la_vq_pages + VIRTIO_RING_SIZE * sizeof(la_vring_desc_t));
    la_used  = (la_used_area_t *)(la_vq_pages + 2 * 4096);

    for (int i = 0; i < VIRTIO_NUM; i++)
        la_desc_free[i] = 1;

    la_used_idx = 0;

    /* Step 6: Driver OK — device can now process requests */
    VR8(VIRTIO_PCI_STATUS) = VIRTIO_CONFIG_S_ACKNOWLEDGE
                           | VIRTIO_CONFIG_S_DRIVER
                           | VIRTIO_CONFIG_S_FEATURES_OK
                           | VIRTIO_CONFIG_S_DRIVER_OK;

    la_uart_puts("    status=");
    la_uart_put_hex(VR8(VIRTIO_PCI_STATUS));
    la_uart_puts("\n");

    #undef VR32
    #undef VR16
    #undef VR8

    la_uart_puts("  virtio: block device ready\n");
}

/* ---- Read one 4KB block (polling) ---- */
int la_virtio_blk_read(uint32_t block_num, void *buf)
{
    if (!la_virtio_base)
        return -1;

    int idx[3];
    if (la_alloc3_desc(idx) < 0)
        return -1;

    static struct la_virtio_blk_outhdr hdr;
    hdr.type     = VIRTIO_BLK_T_IN;
    hdr.reserved = 0;
    hdr.sector   = (uint64_t)block_num * (LA_BLOCK_SIZE / 512);

    static volatile uint8_t status;
    status = 0;

    /* Descriptor 0: request header (device reads) */
    la_desc[idx[0]].addr  = (uint64_t)&hdr;
    la_desc[idx[0]].len   = sizeof(hdr);
    la_desc[idx[0]].flags = VRING_DESC_F_NEXT;
    la_desc[idx[0]].next  = idx[1];

    /* Descriptor 1: data buffer (device writes) */
    la_desc[idx[1]].addr  = (uint64_t)buf;
    la_desc[idx[1]].len   = LA_BLOCK_SIZE;
    la_desc[idx[1]].flags = VRING_DESC_F_WRITE | VRING_DESC_F_NEXT;
    la_desc[idx[1]].next  = idx[2];

    /* Descriptor 2: status byte (device writes) */
    la_desc[idx[2]].addr  = (uint64_t)&status;
    la_desc[idx[2]].len   = 1;
    la_desc[idx[2]].flags = VRING_DESC_F_WRITE;
    la_desc[idx[2]].next  = 0;

    /* Publish to available ring */
    la_avail[2 + (la_avail[1] % VIRTIO_RING_SIZE)] = idx[0];
    asm volatile("dbar 0" ::: "memory");
    la_avail[1] = la_avail[1] + 1;
    asm volatile("dbar 0" ::: "memory");

    /* Notify device */
    *(volatile uint16_t *)(la_virtio_base + VIRTIO_PCI_QUEUE_NOTIFY) = 0;

    /* Poll used ring until completion (with timeout) */
    {
        int timeout = 1000000;
        while ((la_used_idx % VIRTIO_RING_SIZE) == (la_used->id % VIRTIO_RING_SIZE)) {
            asm volatile("" ::: "memory");
            if (--timeout == 0) {
                la_free_chain(idx[0]);
                return -1;
            }
        }
    }

    int id = la_used->elems[la_used_idx].id;
    (void)id;
    la_used_idx = (la_used_idx + 1) % VIRTIO_RING_SIZE;

    la_free_chain(idx[0]);

    return (status == 0) ? 0 : -1;
}

/* ---- Write one 4KB block (polling) ---- */
int la_virtio_blk_write(uint32_t block_num, const void *buf)
{
    if (!la_virtio_base)
        return -1;

    int idx[3];
    if (la_alloc3_desc(idx) < 0)
        return -1;

    struct la_virtio_blk_outhdr hdr;
    hdr.type     = VIRTIO_BLK_T_OUT;
    hdr.reserved = 0;
    hdr.sector   = (uint64_t)block_num * (LA_BLOCK_SIZE / 512);

    static volatile uint8_t status;
    status = 0;

    /* Descriptor 0: request header */
    la_desc[idx[0]].addr  = (uint64_t)&hdr;
    la_desc[idx[0]].len   = sizeof(hdr);
    la_desc[idx[0]].flags = VRING_DESC_F_NEXT;
    la_desc[idx[0]].next  = idx[1];

    /* Descriptor 1: data buffer (device reads) */
    la_desc[idx[1]].addr  = (uint64_t)buf;
    la_desc[idx[1]].len   = LA_BLOCK_SIZE;
    la_desc[idx[1]].flags = VRING_DESC_F_NEXT;   /* no WRITE → device reads */
    la_desc[idx[1]].next  = idx[2];

    /* Descriptor 2: status byte */
    la_desc[idx[2]].addr  = (uint64_t)&status;
    la_desc[idx[2]].len   = 1;
    la_desc[idx[2]].flags = VRING_DESC_F_WRITE;
    la_desc[idx[2]].next  = 0;

    la_avail[2 + (la_avail[1] % VIRTIO_RING_SIZE)] = idx[0];
    asm volatile("dbar 0" ::: "memory");
    la_avail[1] = la_avail[1] + 1;

    *(volatile uint16_t *)(la_virtio_base + VIRTIO_PCI_QUEUE_NOTIFY) = 0;

    {
        int timeout = 1000000;
        while ((la_used_idx % VIRTIO_RING_SIZE) == (la_used->id % VIRTIO_RING_SIZE)) {
            if (--timeout == 0) {
                la_free_chain(idx[0]);
                return -1;
            }
        }
    }

    int id = la_used->elems[la_used_idx].id;
    (void)id;
    la_used_idx = (la_used_idx + 1) % VIRTIO_RING_SIZE;

    la_free_chain(idx[0]);

    return (status == 0) ? 0 : -1;
}
