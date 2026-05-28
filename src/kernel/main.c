#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"
#include "proc/mod.h"
#include "fs/mod.h"

volatile static int started = 0;
volatile static int booting = 0;

int main()
{
    int cpuid = r_tp();

    if (__sync_bool_compare_and_swap(&booting, 0, 1)) {

        print_init();
        printf("cpu %d is booting!\n", cpuid);

        pmem_init();
        kvm_init();
        kvm_inithart();
        mmap_init();
        printf("calling virtio_disk_init()\n");
        virtio_disk_init(); // 增加初始化磁盘
        proc_init();
        printf("before proc_make_first()\n");
        proc_make_first();
        printf("after proc_make_first()\n");
        trap_kernel_init();
        printf("after trap_kernel_init()\n");
        trap_kernel_inithart();
        printf("after trap_kernel_inithart()\n");

        __sync_synchronize();
        started = 1;

    } else {

        while (started == 0)
            ;
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        kvm_inithart();
        trap_kernel_inithart();
    }

    printf("enter proc_scheduler()\n");
    proc_scheduler();

    panic("main: never back!");
    return 0;
}