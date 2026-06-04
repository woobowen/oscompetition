#include "../arch/mod.h"
#include "../trap/mod.h"
#include "../lib/mod.h"

// 每个CPU在运行操作系统时需要一个初始的函数栈
__attribute__((aligned(16))) uint8 CPU_stack[4096 * NCPU];

extern void main();
extern void timer_vector();  // M-mode时钟中断处理程序，在trap.S中定义
extern void kernel_vector(); // S-mode陷阱处理程序，在trap.S中定义

struct mscratch_area {
    uint64 a1_saved;
    uint64 a2_saved;
    uint64 a3_saved;
    uint64 mtimecmp_addr;
    uint64 interval;
} scratch_areas[NCPU];// 每个CPU的mscratch保存区域

// 时钟中断初始化函数
void clockinit() {
    int id = r_mhartid();
    // 1. 为当前CPU分配并初始化mscratch保存区域
    struct mscratch_area *area = &scratch_areas[id];
    area->mtimecmp_addr = CLINT_MTIMECMP(id);
    area->interval = INTERVAL;

    // 2. 设置 mscratch寄存器 指向保存区域
    w_mscratch((uint64)area);
    
    // 3. 设置 M-mode 陷阱向量为时钟中断处理程序
    w_mtvec((uint64)timer_vector);
    
    // 4. 设置第一次时钟中断时间
    uint64 now = r_time();//在arch/method.h中定义
    uint64 *mtimecmp = (uint64*)CLINT_MTIMECMP(id); //每个cpu核的比较寄存器
    *mtimecmp = now + INTERVAL;
    
    // 5. 开启 M-mode 时钟中断
    w_mie(r_mie() | MIE_MTIE);  // MIE_MTIE = (1 << 7)
}

void start()
{
    // 暂时不开启分页，使用物理地址
    w_satp(0);

    // 切换到S-mode后无法访问M-mode的寄存器
    // 所以需要将hartid存到可访问的寄存器tp
    // 之后可以用mycpuid函数访问它
    int id = r_mhartid();
    w_tp(id);

    // 委托S-mode处理所有trap
    w_medeleg(0xffff);//委托异常
    w_mideleg(0xffff);//委托中断
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);//s-mode开启三种中断


    // 时钟中断初始化 (唯一需要在M-mode处理的中断)
    clockinit();

    // 修改mstatus寄存器，假装上一个状态是S-mode
    uint64 status = r_mstatus();
    status &= ~MSTATUS_MPP_MASK;
    status |= MSTATUS_MPP_S;
    w_mstatus(status);

    // 设置M-mode的返回地址
    w_mepc((uint64)main);

    // 触发状态迁移，回到上一个状态（M-mode->S-mode）
    asm volatile ("mret");
}

// Entry point when booted by firmware/OpenSBI into S-mode.
// hartid is passed in a0 by the firmware (entry.S ensures this).
void boot_start(uint64 id)
{
    uart_puts_early("[boot_start-enter]\n");
    // Ensure paging is disabled and we operate in physical addressing
    w_satp(0);

    // set tp to hart id for later use
    w_tp(id);
    uart_puts_early("[boot_start-after-tp]\n");

    // Set S-mode trap vector to kernel's trap handler
    w_stvec((uint64)kernel_vector);

    // Enable S-mode interrupts (software, timer, external)
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

    // Initialize SBI timer (request first S-mode timer interrupt)
    timer_init_sbi();

    // Call the common main entry
    // early boot marker: may run before uart_init
    uart_puts_early("[boot_start]\n");
    main();

    // If main returns, halt
    for(;;) asm volatile("wfi");
}