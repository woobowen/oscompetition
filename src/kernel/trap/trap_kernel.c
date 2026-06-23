#include "mod.h"

// 中断信息
char *interrupt_info[16] = {
    "U-mode software interrupt",      // 0
    "S-mode software interrupt",      // 1
    "reserved-1",                     // 2
    "M-mode software interrupt",      // 3
    "U-mode timer interrupt",         // 4
    "S-mode timer interrupt",         // 5
    "reserved-2",                     // 6
    "M-mode timer interrupt",         // 7
    "U-mode external interrupt",      // 8
    "S-mode external interrupt",      // 9
    "reserved-3",                     // 10
    "M-mode external interrupt",      // 11
    "reserved-4",                     // 12
    "reserved-5",                     // 13
    "reserved-6",                     // 14
    "reserved-7",                     // 15
};

// 异常信息
char *exception_info[16] = {
    "Instruction address misaligned", // 0
    "Instruction access fault",       // 1
    "Illegal instruction",            // 2
    "Breakpoint",                     // 3
    "Load address misaligned",        // 4
    "Load access fault",              // 5
    "Store/AMO address misaligned",   // 6
    "Store/AMO access fault",         // 7
    "Environment call from U-mode",   // 8
    "Environment call from S-mode",   // 9
    "reserved-1",                     // 10
    "Environment call from M-mode",   // 11
    "Instruction page fault",         // 12
    "Load page fault",                // 13
    "reserved-2",                     // 14
    "Store/AMO page fault",           // 15
};


// 实现位于 trap.S
// 它是完整的内核态trap处理流程
extern void kernel_vector();

// kernel_vector() 使用的每 CPU 中断栈与原始 sp 保存区
__attribute__((aligned(PGSIZE))) uint8 kernel_trap_stack[NCPU][PGSIZE];
uint64 kernel_trap_saved_sp[NCPU];

// 初始化trap中各个核心共享的东西
void trap_kernel_init()
{
    plic_init();
    timer_create();
}

// 初始化trap中各个核心独有的东西
void trap_kernel_inithart()
{
    plic_inithart();
    w_stvec((uint64)kernel_vector);
    intr_on();
}

// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler()
{
    //printf("[IRQ] Entering trap_kernel_handler!\n");
    uint64 sepc = r_sepc();       // 记录了发生异常时的PC值
    uint64 sstatus = r_sstatus(); // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();   // 引发trap的原因
    uint64 stval = r_stval();     // 发生trap时保存的附加信息 (不同trap类型不一样)
    uint64 *regs = (uint64 *)(kernel_trap_stack[mycpuid()] + PGSIZE - 256);

    // 确认trap来自S-mode且此时trap处于关闭状态
    assert(sstatus & SSTATUS_SPP, "trap_kernel_handler: not from s-mode");
    assert(intr_get() == 0, "trap_kernel_handler: interreput enabled");

    int trap_id = scause & 0xf;

    /* 高位bit标识了是中断还是异常 */
    if (scause & 0x8000000000000000ul) {
        // 1-中断处理
        switch (trap_id) // 中断产生原因分类
        {
            case 1: // S-mode软件中断
                timer_interrupt_handler();
                break;
            case 5: // S-mode timer interrupt (SBI/sstc path)
                timer_interrupt_handler();
                break;
            case 9: // S-mode外设中断
                external_interrupt_handler();
                break;

            default: // 例外处理
                printf("\nunexpected interrupt: %s\n", interrupt_info[trap_id]);
                printf("trap_id = %d, sepc = %p, stval = %p\n", trap_id, sepc, stval);
                panic("trap_kernel_handler");
        }
    } else {
        // 2-异常处理
        switch (trap_id) // 异常产生原因分类
        {
            //! 本实验目前还未涉及异常处理的内容
        default: // 例外处理
            printf("\nunexpected exception: %s\n", exception_info[trap_id]);
            printf("trap_id = %d, sepc = %p, stval = %p\n", trap_id, sepc, stval);
            printf("trap_regs: ra=%p sp=%p s0=%p a0=%p a1=%p a2=%p\n",
                   regs[0], kernel_trap_saved_sp[mycpuid()], regs[7],
                   regs[9], regs[10], regs[11]);
            uint64 fp = regs[7];
            for (int i = 0; i < 6 && fp != 0; i++) {
                uint64 saved_ra = *((uint64 *)(fp - 8));
                uint64 saved_fp = *((uint64 *)(fp - 16));
                printf("bt%d: ra=%p fp=%p\n", i, saved_ra, saved_fp);
                if (saved_fp <= fp)
                    break;
                fp = saved_fp;
            }
            panic("trap_kernel_handler");
        }
    }
}

// 外设中断处理 (基于PLIC，lab-3 只需要识别和处理UART中断)
void external_interrupt_handler()
{
    // 获取中断号
    int irq = plic_claim();
    
    // 根据中断号进行处理
    switch (irq){
        case 0:// 没有中断
            break;
        case UART_IRQ:// UART输入中断的中断号 定义在lib/type.h中
            //uart_putc_sync('U');
            uart_intr();
            break;
        // lab-7 新增处理磁盘中断
        case VIRTIO_IRQ:// 虚拟磁盘中断的中断号 定义在fs/type.h中
            virtio_disk_intr();
            break;
        default:
            printf("\nunexpected external interrupt irq=%d\n", irq);// 其他中断暂时不处理
            break;
    }

    // 告知PLIC中断处理完成
    plic_complete(irq);
}

// 时钟中断处理 (基于CLINT 或 SBI timer)
void timer_interrupt_handler()
{
    // 由于sys_timer是共享资源, 但每个CPU都能收到时钟中断
    // 所以只需要指定一个CPU(CPU-0)负责更新时钟
    if(mycpuid() == 0)
        timer_update();

    // 调度统计：每个时钟tick，对当前RUNNING进程累计一次CPU占用
    // 这样同时覆盖用户态与内核态执行时间；不加锁避免在中断上下文死锁。
    proc_t *p = myproc();
    if (p && p->state == RUNNING)
        p->sched_cpu_ticks++;

    if (mycpu()->noff == 0) {
        proc_check_itimers(r_time());
        proc_check_sleep_deadlines(r_time());
    }

    // 调度下一次 SBI 定时器中断并清除待决位
    timer_init_sbi();
    w_sip(r_sip() & ~2);
}
