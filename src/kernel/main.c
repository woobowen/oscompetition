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
    if (__sync_bool_compare_and_swap(&booting, 0, 1)) {

        print_init();

        pmem_init();
        kvm_init();
        kvm_inithart();
        mmap_init();
        virtio_disk_init();
        proc_init();
        proc_make_first();
        trap_kernel_init();
        trap_kernel_inithart();

        __sync_synchronize();
        started = 1;

    } else {

        while (started == 0)
            ;
        __sync_synchronize();
        kvm_inithart();
        trap_kernel_inithart();
    }

    proc_scheduler();

    panic("main: never back!");
    return 0;
}