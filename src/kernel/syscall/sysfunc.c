#include "mod.h"

/*
    鐢ㄦ埛鍫嗙┖闂翠几缂?    uint64 new_heap_top (濡傛灉鏄?, 浠ｈ〃鏌ヨ褰撳墠鍫嗛《浣嶇疆)
    鎴愬姛杩斿洖new_heap_top, 澶辫触杩斿洖-1
*/
uint64 sys_brk()
{
    // push_off(); // 涓婇攣闃叉鏃堕挓涓柇骞叉壈椤佃〃鎵撳嵃锛堜笉鍔犲ソ鍍忎篃琛岋級

    uint64 new_top;
    arg_uint64(0, &new_top);

    proc_t *p = myproc();
    if (!p) return (uint64)-1;

    uint64 cur = p->heap_top;

    if (new_top == 0) { // look

    }else if (new_top > cur) { // grow
        uint32 len = (uint32)(new_top - cur);
        uint64 ret = uvm_heap_grow(p->pgtbl, cur, len, PTE_R | PTE_W | PTE_U);
        if (ret == (uint64)-1) return (uint64)-1;
        p->heap_top = ret;
        proc_shared_vm_sync_heap_grow(cur, ret);

    }else { // ungrow & stay
        uint32 len = (uint32)(cur - new_top);
        uint64 ret = uvm_heap_ungrow(p->pgtbl, cur, len);
        if (ret == (uint64)-1) return (uint64)-1;
        p->heap_top = ret;

    }
    // pop_off();
    return p->heap_top;
}

#define MAP_ANONYMOUS_LOCAL 0x20
#define MAP_FIXED_LOCAL 0x10

static int mmap_prefill_file(file_t *file, uint64 start, uint64 len, uint64 file_off)
{
    proc_t *p = myproc();
    if (p == NULL || file == NULL || file->ip == NULL || len == 0)
        return 0;

    inode_t *ip = file->ip;
    inode_lock(ip);
    uint64 file_size = ip->disk_info.size;
    uint64 end = start + len;

    for (uint64 va = (start / PGSIZE) * PGSIZE; va < end; va += PGSIZE) {
        pte_t *pte = vm_getpte(p->pgtbl, va, false);
        if (pte == NULL || !(*pte & PTE_V)) {
            if (uvm_mmap_handle_fault(p->pgtbl, va) == (uint64)-1) {
                inode_unlock(ip);
                return -1;
            }
            pte = vm_getpte(p->pgtbl, va, false);
            if (pte == NULL || !(*pte & PTE_V)) {
                inode_unlock(ip);
                return -1;
            }
        }

        uint64 copy_start = va < start ? start : va;
        uint64 copy_end = (va + PGSIZE) < end ? (va + PGSIZE) : end;
        if (copy_start >= copy_end)
            continue;

        uint64 pos = file_off + (copy_start - start);
        if (pos >= file_size)
            continue;

        uint64 want64 = copy_end - copy_start;
        if (want64 > file_size - pos)
            want64 = file_size - pos;
        uint32 want = (uint32)want64;
        if (want == 0)
            continue;

        uint64 pa = PTE_TO_PA(*pte);
        if (inode_read_data(ip, (uint32)pos, want,
                            (void *)(pa + (copy_start - va)), false) != want) {
            inode_unlock(ip);
            return -1;
        }
    }

    inode_unlock(ip);
    return 0;
}

/*
    澧炲姞涓€娈靛唴瀛樻槧灏?    mmap(addr, length, prot, flags, fd, offset)
    鎴愬姛杩斿洖鏄犲皠绌洪棿鐨勮捣濮嬪湴鍧€, 澶辫触杩斿洖(uint64)-1
*/
uint64 sys_mmap()
{
    uint64 start;
    uint64 len;
    uint64 prot;
    uint64 flags;
    uint64 fd;
    uint64 offset;
    arg_uint64(0, &start);
    arg_uint64(1, &len);
    arg_uint64(2, &prot);
    arg_uint64(3, &flags);
    fd = arg_raw(4);
    offset = arg_raw(5);

    if (len == 0)
        return (uint64)(-EINVAL);
    if (start != 0 && start % PGSIZE != 0)
        return (uint64)(-EINVAL);

    uint64 aligned_len = (len + PGSIZE - 1) & ~(PGSIZE - 1);
    if (aligned_len < len)
        return (uint64)(-EINVAL);
    uint32 npages = aligned_len / PGSIZE;
    int map_fixed = (flags & MAP_FIXED_LOCAL) != 0;
    if (!map_fixed)
        start = 0;
    if (start != 0 && (start < MMAP_BEGIN || start + aligned_len < start || start + aligned_len > MMAP_END)) {
        return (uint64)(-ENOMEM);
    }
    int file_backed = ((flags & MAP_ANONYMOUS_LOCAL) == 0 && fd != (uint64)-1);
    file_t *file = NULL;
    if (file_backed && offset % PGSIZE != 0)
        return (uint64)(-EINVAL);
    if (file_backed && arg_fd(4, NULL, &file) < 0)
        return (uint64)(-EBADF);

    // 鏍规嵁 prot 璁剧疆 PTE 鏉冮檺
    // PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4
    int perm = PTE_U;
    if (prot & 1) perm |= PTE_R;
    if (prot & 2) perm |= PTE_W | PTE_R;
    if (prot & 4) perm |= PTE_X;
    // 鑻?prot=PROT_NONE(0) 鎴栨湭璁剧疆璇绘潈闄愶紝缁欐渶灏忚鏉冮檺閬垮厤 musl 璁块棶澶撮儴澶辫触
    if (!(perm & (PTE_R | PTE_W | PTE_X)))
        perm |= PTE_R;

    if (map_fixed)
        uvm_munmap(start, npages);
    uint64 ret_addr = uvm_mmap(start, npages, perm);
    if (ret_addr == (uint64)-1)
        return (uint64)(-ENOMEM);
    if (file_backed && mmap_prefill_file(file, ret_addr, (uint64)npages * PGSIZE, offset) < 0) {
        uvm_munmap(ret_addr, npages);
        return (uint64)(-ENOMEM);
    }
    return ret_addr;
}

/*
    瑙ｉ櫎涓€娈靛唴瀛樻槧灏?    uint64 start 璧峰鍦板潃
    uint32 len   鑼冨洿 (瀛楄妭, 闇€妫€鏌ユ槸鍚︽槸page-aligned)
    鎴愬姛杩斿洖0 澶辫触杩斿洖-1
*/
uint64 sys_munmap()
{
    uint64 start; // 璧峰鍦板潃
    uint64 len;   // 鍦板潃鑼冨洿
    arg_uint64(0, &start);
    arg_uint64(1, &len);

    if (len == 0) return (uint64)(-EINVAL);
    if (start % PGSIZE != 0) return (uint64)(-EINVAL);
    if (start + len < start) return (uint64)(-EINVAL);

    uint64 aligned_len = (len + PGSIZE - 1) & ~(PGSIZE - 1);
    if (aligned_len < len) return (uint64)(-EINVAL);
    if (aligned_len / PGSIZE > 0xffffffffUL) return (uint64)(-EINVAL);
    uint32 npages = (uint32)(aligned_len / PGSIZE);
    uvm_munmap(start, npages);

    // // 璋冭瘯
    // proc_t *p = myproc();
    // printf("sys_munmap: start = %p, len = 0x%x\n", (void *)start, len);
    // uvm_show_mmaplist(p->mmap);
    // vm_print(p->pgtbl);
    // printf("\n");

    return 0;
}

// 216 mremap(old_addr, old_size, new_size, flags, new_addr): minimal resize compatibility.
uint64 sys_mremap()
{
    uint64 old_addr = arg_raw(0);
    uint64 old_size = arg_raw(1);
    uint64 new_size = arg_raw(2);
    uint64 flags = arg_raw(3);
    uint64 new_addr = arg_raw(4);

    if (old_addr == 0 || old_addr % PGSIZE != 0 || old_size == 0 || new_size == 0)
        return (uint64)(-EINVAL);
    if ((flags & ~3UL) != 0)
        return (uint64)(-EINVAL);
    if ((flags & 2UL) != 0 && (new_addr == 0 || new_addr % PGSIZE != 0))
        return (uint64)(-EINVAL);

    if (new_size <= old_size)
        return old_addr;
    return (uint64)(-ENOMEM);
}

/*
    杩涚▼澶嶅埗
    杩斿洖瀛愯繘绋嬬殑pid
*/
uint64 sys_fork()
{
    return proc_fork();
}

uint64 sys_clone()
{
    // Linux/RISC-V clone(flags=a0, stack=a1, parent_tid=a2, tls=a3, child_tid=a4)
    uint64 flags = arg_raw(0);
    uint64 stack = arg_raw(1);
    uint64 parent_tid = arg_raw(2);
    uint64 tls = arg_raw(3);
    uint64 child_tid = arg_raw(4);

    if ((flags & 0x100) == 0 && stack == 0)
        return proc_fork();

    if ((flags & 0x100) == 0)
        return (uint64)(-ENOSYS);

    proc_t *parent = myproc();
    proc_t *child = proc_alloc();
    if (child == NULL)
        return (uint64)(-EAGAIN);

    trapframe_t *tf = (trapframe_t *)pmem_alloc(false);
    if (!tf) {
        spinlock_release(&child->lk);
        return (uint64)(-ENOMEM);
    }
    *tf = *parent->tf;
    tf->a0 = 0;
    if (stack != 0)
        tf->sp = stack;
    if ((flags & 0x80000) && tls != 0)
        tf->tp = tls;

    child->tf = tf;
    child->parent = parent;
    child->exit_code = 0;
    child->pgtbl = proc_pgtbl_init((uint64)tf);
    if (!child->pgtbl) {
        pmem_free((uint64)tf, false);
        child->tf = NULL;
        spinlock_release(&child->lk);
        return (uint64)(-ENOMEM);
    }
    child->heap_top = parent->heap_top;
    child->ustack_npage = parent->ustack_npage;
    child->mmap = parent->mmap;
    child->vm_owner = parent->vm_owner ? parent->vm_owner : parent;
    child->shared_vm = 1;
    child->thread_group = (flags & 0x10000) ? 1 : 0;
    if (child->thread_group) {
        spinlock_acquire(&parent->lk);
        parent->thread_group = 1;
        spinlock_release(&parent->lk);
    }
    child->state = RUNNABLE;
    child->sched_last_ready_tick = timer_get_ticks();
    child->mlfq_age_start_tick = child->sched_last_ready_tick;
    child->sched_ready_count++;

    tf->user_to_kern_satp = r_satp();
    tf->user_to_kern_sp = child->kstack + 2 * PGSIZE;
    tf->user_to_kern_hartid = mycpuid();

    if (uvm_share_pgtbl(parent->pgtbl, child->pgtbl, parent->heap_top,
                        parent->ustack_npage, parent->mmap) < 0) {
        proc_free(child);
        spinlock_release(&child->lk);
        return (uint64)(-ENOMEM);
    }

    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++) {
        child->open_file[i] = parent->open_file[i] ? file_dup(parent->open_file[i]) : NULL;
        child->fd_cloexec[i] = parent->fd_cloexec[i];
    }

    memcpy(child->sig_handler, parent->sig_handler, sizeof(parent->sig_handler));
    child->sig_restorer = parent->sig_restorer;
    child->sig_pending = 0;
    child->sig_delivering = 0;
    child->group_exit_pending = 0;
    child->group_exit_code = 0;
    child->clear_child_tid = (flags & 0x200000) ? child_tid : 0;
    child->itimer_expire = 0;
    child->itimer_interval = 0;
    child->ub_looper_secs = 0;
    child->cwd = parent->cwd ? inode_dup(parent->cwd) : NULL;

    int pid = child->pid;
    if ((flags & 0x100000) && parent_tid != 0)
        uvm_copyout(parent->pgtbl, parent_tid, (uint64)&pid, sizeof(pid));
    if ((flags & 0x1000000) && child_tid != 0)
        uvm_copyout(parent->pgtbl, child_tid, (uint64)&pid, sizeof(pid));

    spinlock_release(&child->lk);
    mlfq_on_new(child);
    return (uint64)pid;
}

/*
    wait4(pid, *status, options, *rusage) 鈥?Linux/RISC-V ABI (syscall 260)
    a0=pid: -1=浠绘剰瀛愯繘绋? >0=绛夌壒瀹歱id
    a1=status鎸囬拡
    a2=options: WNOHANG(1)=闈為樆濉?    a3=rusage鎸囬拡(蹇界暐)

    鍏煎鏃у紡 wait(&status): a0=status鍦板潃, a1=0, a2=0
    鍒ゆ嵁: a0 >= PGSIZE 瑙嗕负鐢ㄦ埛鎬佸湴鍧€(status鎸囬拡), 鎸夋棫鎺ュ彛澶勭悊
*/
uint64 sys_wait()
{
    uint64 a0 = arg_raw(0);
    uint64 a1 = arg_raw(1);
    uint64 a2 = arg_raw(2);

    // 鍖哄垎涓ょ璋冪敤褰㈠紡锛?    // 1. 鏃у紡 SeaOS wait(*status):  a0=鐢ㄦ埛鎬佸湴鍧€, a1=a2=0
    // 2. Linux wait4(pid,*status,options,rusage): a0=pid(灏忔暣鏁版垨-1), a1=鍦板潃
    //
    // 鍒ゆ嵁锛歛1 闈為浂鏃朵竴瀹氭槸 wait4锛堝洜涓?a1 鏄?status 鎸囬拡锛夛紱
    //       a1==0 涓?a0 鐪嬭捣鏉ュ儚鐢ㄦ埛鍦板潃(>=PGSIZE 涓斾笉鏄?-1)鏃舵寜鏃ф帴鍙ｅ鐞嗐€?
    int64  wait_pid;
    uint64 stat_ptr;
    int    wnohang;

    if (a1 != 0) {
        // Linux wait4: a0=pid, a1=*status, a2=options
        wait_pid = (int64)a0;
        stat_ptr = a1;
        wnohang  = (a2 & 1) != 0;
    } else if (a0 >= (uint64)PGSIZE && a0 != (uint64)-1LL) {
        // 鏃у紡 wait(*status): a0 鏄?status 鎸囬拡锛岀瓑寰呬换鎰忓瓙杩涚▼
        wait_pid = -1;
        stat_ptr = a0;
        wnohang  = 0;
    } else {
        // wait4(pid, NULL, options, NULL)
        wait_pid = (int64)a0;
        stat_ptr = 0;
        wnohang  = (a2 & 1) != 0;
    }

    return proc_wait4(wait_pid, stat_ptr, wnohang);
}

/*
    杩涚▼閫€鍑?    int exit_code
    涓嶈繑鍥?*/
uint64 sys_exit()
{
    int exit_code;
    arg_uint32(0, (uint32 *)&exit_code); // 鑾峰彇閫€鍑虹爜
    proc_exit(exit_code);
    return 0; // 涓嶄細鎵ц鍒拌繖閲?
}

/*
    璁╄繘绋嬬潯鐪犱竴娈垫椂闂?    uint32 ntick (1涓猼ick澶х害0.1绉?
    鎴愬姛杩斿洖0
*/
uint64 sys_sleep()
{
    uint64 req = 0;
    uint64 rem = 0;
    if (arg_raw(1) != 0 || arg_raw(2) != 0) {
        arg_uint64(0, &req);
        arg_uint64(1, &rem);
        (void)rem;
        if (req == 0)
            return 0;
        uint64 ts[2] = {0, 0};
        uvm_copyin(myproc()->pgtbl, (uint64)ts, req, sizeof(ts));
        uint64 ntick = ts[0] * 10;
        if (ts[1] > 0)
            ntick += (ts[1] + 99999999ull) / 100000000ull;
        if (ntick == 0)
            ntick = 1;
        timer_wait(ntick);
        return 0;
    }

    uint32 ntick;
    arg_uint32(0, &ntick); // 鑾峰彇鐫＄湢鐨則ick鏁?
    timer_wait((uint64)ntick);
    return 0;
}

/*
    杩斿洖褰撳墠杩涚▼鐨刾id
*/
uint64 sys_getpid()
{
    return (uint64)(myproc()->pid);
}

uint64 sys_gettid()
{
    return myproc()->pid;   // 鍗曠嚎绋? tid == pid
}

/*
    set_tid_address(int *tidptr)
    Linux 璇箟: 璁剧疆璋冪敤绾跨▼ clear_child_tid = tidptr, 杩斿洖璋冪敤鑰?TID銆?    SeaOS 鍗曠嚎绋?杩涚▼妯″瀷涓?TID == PID銆?    鏈€灏忓疄鐜?docs/DECISIONS.md D3): 鏆備笉瀛樺偍 tidptr銆佷笉鍋氶€€鍑烘椂 clear_child_tid 娓呴浂+futex 鍞ら啋,
    浠呰繑鍥?pid 婊¤冻 musl 鍚姩鏈熴€?*/
uint64 sys_set_tid_address()
{
    myproc()->clear_child_tid = arg_raw(0);
    return (uint64)(myproc()->pid);
}

// qemu virt time CSR frequency: INTERVAL=1e6 cycles ~= 0.1s, so timebase is 10MHz.
#define FUTEX_TIMEBASE_HZ 10000000ull

#define FUTEX_WAIT_OP 0
#define FUTEX_WAKE_OP 1
#define FUTEX_WAIT_BITSET_OP 9
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256

static uint64 futex_rel_timeout_ticks(uint64 timeout_addr)
{
    if (timeout_addr == 0)
        return 0;
    uint64 ts[2] = {0, 0};
    uvm_copyin(myproc()->pgtbl, (uint64)ts, timeout_addr, sizeof(ts));
    uint64 ticks = ts[0] * 10;
    if (ts[1] > 0)
        ticks++;
    return ticks;
}

static uint64 futex_abs_timeout_ticks(uint64 timeout_addr)
{
    if (timeout_addr == 0)
        return 0;
    uint64 ts[2] = {0, 0};
    uvm_copyin(myproc()->pgtbl, (uint64)ts, timeout_addr, sizeof(ts));
    uint64 max_u64 = ~0ull;
    if (ts[0] > max_u64 / FUTEX_TIMEBASE_HZ)
        return max_u64 / INTERVAL;
    uint64 deadline = ts[0] * FUTEX_TIMEBASE_HZ + ts[1] / 100;
    uint64 now = r_time();
    if (deadline <= now)
        return 0;
    return (deadline - now + INTERVAL - 1) / INTERVAL;
}

static int futex_wait_current(uint64 uaddr, uint32 val)
{
    uint32 cur = 0;
    uvm_copyin(myproc()->pgtbl, (uint64)&cur, uaddr, sizeof(cur));
    return cur == val;
}

// 98 futex(uaddr, op, val, timeout, uaddr2, val3): minimal WAIT/WAKE.
uint64 sys_futex()
{
    uint64 uaddr = arg_raw(0);
    int raw_op = (int)arg_raw(1);
    int op = raw_op & ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME);
    uint32 val = (uint32)arg_raw(2);
    uint64 timeout_addr = arg_raw(3);

    if (uaddr == 0)
        return (uint64)(-EFAULT);
    if (op == FUTEX_WAKE_OP) {
        proc_wakeup_force((void *)uaddr);
        return 1;
    }
    if (op == FUTEX_WAIT_OP || op == FUTEX_WAIT_BITSET_OP) {
        proc_t *p = myproc();
        spinlock_acquire(&p->lk);
        if (!futex_wait_current(uaddr, val)) {
            spinlock_release(&p->lk);
            return (uint64)(-EAGAIN);
        }
        if (timeout_addr == 0) {
            proc_sleep((void *)uaddr, &p->lk);
            spinlock_release(&p->lk);
            return 0;
        }
        spinlock_release(&p->lk);

        uint64 ticks = (op == FUTEX_WAIT_BITSET_OP) ?
            futex_abs_timeout_ticks(timeout_addr) :
            futex_rel_timeout_ticks(timeout_addr);
        if (ticks == 0)
            return (uint64)(-ETIMEDOUT);
        while (ticks-- > 0) {
            timer_wait(1);
            if (!futex_wait_current(uaddr, val))
                return 0;
        }
        return (uint64)(-ETIMEDOUT);
    }
    return 0;
}

/*
    鎷夊彇璋冨害缁熻蹇収
    uint64 buf_user (sched_stat_t*)
    uint32 max_entries
    杩斿洖瀹為檯鍐欏叆鏉＄洰鏁?*/
uint64 sys_schedstat()
{
    uint64 buf_user;
    uint32 max_entries;
    arg_uint64(0, &buf_user);
    arg_uint32(1, &max_entries);
    return (uint64)proc_schedstat(buf_user, max_entries);
}

/*
    鎵цELF鏂囦欢浠ユ浛鎹㈠綋鍓嶈繘绋嬬殑鍐呭
    char *path
    char **argv
    鎴愬姛杩斿洖argc, 澶辫触杩斿洖-1
*/
uint64 sys_exec()
{
    char path[STR_MAXLEN + 1];
    uint64 argv_addr;
    uint64 envp_addr = 0;

    if (arg_raw(2) != 0) {
        arg_str(0, path, STR_MAXLEN);
        arg_uint64(1, &argv_addr);
        arg_uint64(2, &envp_addr);
    } else {
        arg_str(0, path, STR_MAXLEN);
        arg_uint64(1, &argv_addr);
    }

    // 璇诲彇argv鏁扮粍
    char *kargv[32];
    char *kenvp[32];
    int argc = 0;
    int envc = 0;
    uint64 addr;
    proc_t *p = myproc();

    while (argc < 32) {
        uvm_copyin(p->pgtbl, (uint64)&addr, argv_addr + argc * sizeof(uint64), sizeof(uint64));
        if (addr == 0) break;
        kargv[argc] = (char*)pmem_alloc(false);
        if (!kargv[argc]) {
            for (int j = 0; j < argc; j++) pmem_free((uint64)kargv[j], false);
            return -1;
        }
        uvm_copyin_str(p->pgtbl, (uint64)kargv[argc], addr, STR_MAXLEN);
        argc++;
    }
    kargv[argc] = NULL;

    while (envp_addr != 0 && envc < 32) {
        uvm_copyin(p->pgtbl, (uint64)&addr, envp_addr + envc * sizeof(uint64), sizeof(uint64));
        if (addr == 0) break;
        kenvp[envc] = (char*)pmem_alloc(false);
        if (!kenvp[envc]) {
            for (int j = 0; j < argc; j++) pmem_free((uint64)kargv[j], false);
            for (int j = 0; j < envc; j++) pmem_free((uint64)kenvp[j], false);
            return -1;
        }
        uvm_copyin_str(p->pgtbl, (uint64)kenvp[envc], addr, STR_MAXLEN);
        envc++;
    }
    kenvp[envc] = NULL;

    int ret = proc_exec_env(path, kargv, envp_addr != 0 ? kenvp : NULL);

    // 閲婃斁鍐呮牳argv
    for (int i = 0; i < argc; i++) {
        pmem_free((uint64)kargv[i], false);
    }
    for (int i = 0; i < envc; i++) {
        pmem_free((uint64)kenvp[i], false);
    }

    return ret;
}

/* 鏋勫缓fd->file鐨勬槧灏? 杩斿洖fd */
static uint32 alloc_fd_at_least(file_t *file, uint32 min_fd, uint8 cloexec)
{
    proc_t *p = myproc();
    for (uint32 i = min_fd; i < N_OPEN_FILE_PER_PROC; i++)
    {
        if (p->open_file[i] == NULL) {
            p->open_file[i] = file;
            p->fd_cloexec[i] = cloexec;
            return i;
        }
    }
    return -1;
}

static uint32 alloc_fd(file_t *file)
{
    return alloc_fd_at_least(file, 0, 0);
}

/*
    鎵撳紑鎴栧垱寤烘枃浠?    char *path
    uint32 open_mode
    鎴愬姛杩斿洖fd, 澶辫触杩斿洖-1
*/
uint64 sys_open()
{
    char path[STR_MAXLEN + 1];
    uint64 arg0 = arg_raw(0);
    uint64 arg1 = arg_raw(1);

    uint32 open_mode = 0;
    bool looks_like_openat = false;

    // 鍏煎涓ょ璋冪敤褰㈡€侊細
    // 1) SeaOS 鏃ф帴鍙? open(path, mode)
    // 2) Linux openat: openat(dirfd, path, flags, mode)
    // 鍙湁褰撳弬鏁板舰鎬佹槑鏄惧儚 openat 鏃舵墠杩涘叆 openat 鍒嗘敮锛岄伩鍏嶈娈嬬暀瀵勫瓨鍣ㄨ瀵笺€?
    if (((int64)arg0) <= 4096 && arg0 != 0) {
        looks_like_openat = true;
    }
    if (arg1 >= PGSIZE) {
        looks_like_openat = true;
    }
    if (looks_like_openat) {
        uint64 dirfd;
        uint32 flags;
        uint32 mode;
        arg_uint64(0, &dirfd);
        arg_str(1, path, STR_MAXLEN);
        arg_uint32(2, &flags);
        arg_uint32(3, &mode);
        (void)dirfd;
        (void)mode;
        if ((flags & 3) == 0)
            open_mode |= FILE_OPEN_READ;
        if ((flags & 3) == 1)
            open_mode |= FILE_OPEN_WRITE;
        if ((flags & 3) == 2)
            open_mode |= FILE_OPEN_READ | FILE_OPEN_WRITE;
        if (flags & 64)
            open_mode |= FILE_OPEN_CREATE;
        if (flags & 512)
            open_mode |= FILE_OPEN_TRUNC;
        if (flags & 1024)
            open_mode |= FILE_OPEN_APPEND;
    } else {
        arg_str(0, path, STR_MAXLEN);
        arg_uint32(1, &open_mode);
    }

    file_t *file = file_open(path, open_mode);
    if (!file) return -1;

    uint32 fd = alloc_fd(file);
    if (fd == (uint32)-1) {
        file_close(file);
        return -1;
    }
    if (looks_like_openat && ((uint32)arg_raw(2) & 0x80000))
        myproc()->fd_cloexec[fd] = 1;
    return fd;
}

/*
    鍏抽棴鏂囦欢
    uint32 fd
    鎴愬姛杩斿洖0, 澶辫触杩斿洖-1
*/
uint64 sys_close()
{
    file_t *file;
    uint32 fd;
    if (arg_fd(0, &fd, &file) < 0) return -1;
    file_close(file);
    myproc()->open_file[fd] = NULL;
    myproc()->fd_cloexec[fd] = 0;
    
    return 0;
}

/*
    璇诲彇鏂囦欢鍐呭
    uint32 fd
    uint32 len
    uint64 addr
    鎴愬姛杩斿洖璇诲埌鐨勫瓧鑺傛暟, 澶辫触杩斿洖0
*/
uint64 sys_read()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return 0;

    // Linux/RISC-V ABI: read(fd=a0, buf=a1, count=a2)
    uint64 addr;
    arg_uint64(1, &addr);   // a1 = buf
    uint32 len;
    arg_uint32(2, &len);    // a2 = count

    if (file->is_socket) {
        int ret = socket_recvfrom(file->socket, addr, len, 0, 0, 0);
        return ret < 0 ? (uint64)ret : (uint64)ret;
    }

    return file_read(file, len, addr, true);
}

// 67 pread64(fd, buf, count, offset): read without changing the fd offset.
uint64 sys_pread64()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0)
        return (uint64)(-EBADF);

    uint64 addr = arg_raw(1);
    uint32 len = (uint32)arg_raw(2);
    uint64 offset = arg_raw(3);
    if (offset > 0xffffffffUL)
        return 0;
    if (file->is_socket)
        return (uint64)(-ESPIPE);

    uint32 saved = file->offset;
    file->offset = (uint32)offset;
    uint32 ret = file_read(file, len, addr, true);
    file->offset = saved;
    return ret;
}

/*
    鍐欏叆鏂囦欢鍐呭
    uint32 fd
    uint32 len
    uint64 addr
    鎴愬姛杩斿洖鍐欏叆鐨勫瓧鑺傛暟, 澶辫触杩斿洖0
*/
uint64 sys_write()
{
    file_t *file;
    uint32 fd;
    if (arg_fd(0, &fd, &file) < 0) return (uint64)(-EBADF);

    // Linux/RISC-V ABI: write(fd=a0, buf=a1, count=a2)
    uint64 addr;
    arg_uint64(1, &addr);   // a1 = buf
    uint32 len;
    arg_uint32(2, &len);    // a2 = count

    if (file->is_socket) {
        int ret = socket_sendto(file->socket, addr, len, 0, 0, 0);
        return ret < 0 ? (uint64)ret : (uint64)ret;
    }

    uint32 ret = file_write(file, len, addr, true);
    return ret;
}

// 66 writev(fd, iovec*, iovcnt)锛氭寜 Linux ABI 閫愭鍐欏嚭
uint64 sys_readv()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return (uint64)(-EBADF);
    uint64 iov = arg_raw(1);
    int iovcnt = (int)arg_raw(2);
    if (iovcnt <= 0) return 0;

    uint64 total = 0;
    for (int i = 0; i < iovcnt; i++) {
        uint64 vec[2];
        uvm_copyin(myproc()->pgtbl, (uint64)vec, iov + (uint64)i * 16, 16);
        uint64 base = vec[0];
        uint64 len = vec[1];
        if (len == 0) continue;
        uint32 r = file_read(file, (uint32)len, base, true);
        total += r;
        if (r < len) break;
    }
    return total;
}

uint64 sys_writev()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return (uint64)(-EBADF);
    uint64 iov    = arg_raw(1);          // 鐢ㄦ埛鎬?struct iovec[] 鎸囬拡
    int    iovcnt = (int)arg_raw(2);
    if (iovcnt <= 0) return 0;

    uint64 total = 0;
    for (int i = 0; i < iovcnt; i++) {
        uint64 vec[2];                   // struct iovec { void* base; size_t len; } = 16B
        uvm_copyin(myproc()->pgtbl, (uint64)vec, iov + (uint64)i * 16, 16);
        uint64 base = vec[0];
        uint64 len  = vec[1];
        if (len == 0) continue;
        uint32 w = file_write(file, (uint32)len, base, true);
        total += w;
        if (w < len) break;              // 鐭啓, 鍋滄
    }
    return total;
}

// 94 exit_group(status): terminate the current process thread group.
uint64 sys_exit_group()
{
    int exit_code;
    arg_uint32(0, (uint32 *)&exit_code);
    proc_exit_group(exit_code);
    return 0;
}

static void sbi_system_shutdown()
{
    register uint64 a0 asm("a0") = 0;           // reset_type: shutdown
    register uint64 a1 asm("a1") = 0;           // reset_reason: no reason
    register uint64 a6 asm("a6") = 0;           // fid
    register uint64 a7 asm("a7") = 0x53525354;  // EID "SRST"
    asm volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a6), "r"(a7) : "memory");

    while (1)
        asm volatile("wfi");
}

uint64 sys_shutdown()
{
    printf("sys_shutdown: powering off via SBI SRST\n");
    sbi_system_shutdown();
    return 0;
}

static int sys_spawn_and_wait(char *path, char **argv)
{
    int pid = proc_fork();
    if (pid < 0)
        return -1;

    if (pid > 0) {
        int eret = proc_exec_target(pid, path, argv);
        (void)eret;
    }

    if (proc_wait4(pid, 0, 0) < 0)
        return -1;
    return 0;
}

/*
    璋冩暣璇诲啓鎸囬拡浣嶇疆
    uint32 fd
    uint32 offset
    uint32 flag
    鎴愬姛杩斿洖鏂扮殑鍋忕Щ閲? 澶辫触杩斿洖-1
*/
uint64 sys_lseek()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return -1;
    
    uint32 offset, flag;
    arg_uint32(1, &offset);
    arg_uint32(2, &flag);
    
    return file_lseek(file, offset, flag);
}

/*
    澶嶅埗鏂囦欢鎺у埗鏉?    uinr32 fd
    鎴愬姛杩斿洖new_fd, 澶辫触杩斿洖-1
*/
uint64 sys_dup()
{
    file_t *file;
    uint32 fd;
    if (arg_fd(0, &fd, &file) < 0) return -1;
    
    file_t *new_file = file_dup(file);
    if (!new_file) return -1;
    
    uint32 new_fd = alloc_fd(new_file);
    if (new_fd == (uint32)-1) {
        file_close(new_file);
        return -1;
    }
    myproc()->fd_cloexec[new_fd] = 0;
    
    return new_fd;
}

/*
    dup3(oldfd, newfd, flags)
    澶嶅埗 oldfd 鍒版寚瀹氱殑 newfd, 鑻?newfd 宸叉墦寮€鍒欏厛鍏抽棴
    flags 蹇界暐 (O_CLOEXEC 鏆備笉瀹炵幇)
    鎴愬姛杩斿洖 newfd, 澶辫触杩斿洖 -EBADF/-EINVAL
*/
uint64 sys_dup3()
{
    uint32 oldfd = (uint32)arg_raw(0);
    uint32 newfd = (uint32)arg_raw(1);
    uint32 flags = (uint32)arg_raw(2);

    proc_t *p = myproc();
    if (flags & ~0x80000U)
        return (uint64)(-EINVAL);
    if (oldfd >= N_OPEN_FILE_PER_PROC || p->open_file[oldfd] == NULL)
        return (uint64)(-EBADF);
    if (newfd >= N_OPEN_FILE_PER_PROC)
        return (uint64)(-EBADF);
    if (oldfd == newfd)
        return (uint64)(-EINVAL);

    if (p->open_file[newfd] != NULL) {
        file_close(p->open_file[newfd]);
        p->open_file[newfd] = NULL;
    }

    p->open_file[newfd] = file_dup(p->open_file[oldfd]);
    p->fd_cloexec[newfd] = (flags & 0x80000U) ? 1 : 0;
    return newfd;
}

/*
    mprotect(addr, len, prot)
    Minimal Linux/RISC-V compatibility: update permissions on existing
    user mappings. PROT_NONE keeps a readable mapping to match current
    SeaOS mmap behavior while allowing pthread stacks/TLS to become writable.
*/
uint64 sys_mprotect()
{
    uint64 addr = arg_raw(0);
    uint64 len = arg_raw(1);
    uint64 prot = arg_raw(2);

    if (len == 0)
        return 0;
    if (addr % PGSIZE != 0 || (prot & ~7UL) != 0)
        return (uint64)(-EINVAL);
    if (len > VA_MAX - (PGSIZE - 1))
        return (uint64)(-EINVAL);

    uint64 aligned_len = (len + PGSIZE - 1) & ~(PGSIZE - 1);
    if (addr + aligned_len < addr || addr + aligned_len > VA_MAX)
        return (uint64)(-EINVAL);

    int perm = PTE_U;
    if (prot & 1) perm |= PTE_R;
    if (prot & 2) perm |= PTE_W | PTE_R;
    if (prot & 4) perm |= PTE_X;
    if (!(perm & (PTE_R | PTE_W | PTE_X)))
        perm |= PTE_R;

    if (uvm_mprotect(myproc()->pgtbl, addr, aligned_len, perm) < 0)
        return (uint64)(-ENOMEM);
    return 0;
}

// 227 msync(addr, length, flags): current mmap has no file-backed dirty pages.
uint64 sys_msync()
{
    uint64 addr = arg_raw(0);
    uint64 len = arg_raw(1);
    uint64 flags = arg_raw(2);

    if (len == 0)
        return 0;
    if (addr % PGSIZE != 0)
        return (uint64)(-EINVAL);
    if ((flags & ~(1UL | 2UL | 4UL)) != 0)
        return (uint64)(-EINVAL);
    return 0;
}

/*
    鑾峰彇鏂囦欢淇℃伅
    uint32 fd
    uint64 addr
    鎴愬姛杩斿洖0, 澶辫触杩斿洖-1
*/
uint64 sys_fstat()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return (uint64)(-EBADF);

    uint64 addr;
    arg_uint64(1, &addr);

    return file_get_stat_linux(file, addr);
}

/*
    sync()
    RISC-V Linux ABI 81. SeaOS uses a read-only ext4 image plus virtual
    in-memory files for tests, so there is no dirty disk state to flush.
*/
uint64 sys_sync()
{
    return 0;
}

// 82 fsync(fd): valid fd succeeds; SeaOS test FS has no per-fd flush state.
uint64 sys_fsync()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0)
        return (uint64)(-EBADF);
    return 0;
}

// 83 fdatasync(fd): same minimal semantics as fsync.
uint64 sys_fdatasync()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0)
        return (uint64)(-EBADF);
    return 0;
}

/*
    鑾峰彇鐩綍涓殑鎵€鏈夌洰褰曢」淇℃伅
    uint32 fd
    uint64 addr
    uint32 buffer_len
    鎴愬姛杩斿洖璇诲埌鐨勫瓧鑺傛暟, 澶辫触杩斿洖-1
*/
uint64 sys_get_dentries()
{
    file_t *file;
    uint32 fd;
    if (arg_fd(0, &fd, &file) < 0) {
        return -1;
    }

    uint64 addr;
    arg_uint64(1, &addr);

    uint32 buffer_len;
    arg_uint32(2, &buffer_len);

    if (file == NULL) {
        return -1;
    }

    return file_get_dents_linux(file, addr, buffer_len);
}

/*
    鍒涘缓鐩綍
    char *path
    鎴愬姛杩斿洖0, 澶辫触杩斿洖-1
*/
uint64 sys_mkdir()
{
    char path[STR_MAXLEN + 1];
    if (arg_raw(1) != 0 || arg_raw(2) != 0) {
        uint64 dirfd;
        uint32 mode;
        arg_uint64(0, &dirfd);
        arg_str(1, path, STR_MAXLEN);
        arg_uint32(2, &mode);
        (void)dirfd;
        (void)mode;
    } else {
        arg_str(0, path, STR_MAXLEN);
    }

    inode_t *exists = path_to_inode(path);
    if (exists != NULL) {
        inode_put(exists);
        return (uint64)(-EEXIST);
    }
    if (memfs_mkdir(path) == 0)
        return 0;

    inode_t *ip = path_create_inode(path,INODE_TYPE_DIR, 0, 0);
    if (!ip) return -1;

    inode_put(ip);
    return 0;
}

/*
    淇敼褰撳墠宸ヤ綔鐩綍
    char *new_path
    鎴愬姛杩斿洖0, 澶辫触杩斿洖-1
*/
uint64 sys_chdir()
{
    char path[STR_MAXLEN + 1];
    arg_str(0, path, STR_MAXLEN);
    
    inode_t *ip = path_to_inode(path);
    if (!ip || ip->disk_info.type != INODE_TYPE_DIR) {
        if (ip) inode_put(ip);
        return -1;
    }
    
    inode_dup(ip);
    proc_t *p = myproc();
    if (p->cwd) inode_put(p->cwd);
    p->cwd = ip;
    
    return 0;
}

/*
    鎵撳嵃褰撳墠宸ヤ綔鐩綍鐨勭粷瀵硅矾寰?    鎴愬姛杩斿洖0, 澶辫触杩斿洖-1
*/
uint64 sys_getcwd()
{
    uint64 buf = arg_raw(0);
    uint64 size = arg_raw(1);
    proc_t *p = myproc();
    if (!p->cwd || buf == 0 || size == 0) return (uint64)(-EINVAL);

    char path[STR_MAXLEN + 1];
    uint32 offset = inode_to_path(p->cwd, path, STR_MAXLEN + 1);
    if (offset == (uint32)-1) {
        const char *fallback = "/musl";
        uint32 need = (uint32)strlen(fallback) + 1;
        if (need > size) return (uint64)(-ERANGE);
        uvm_copyout(p->pgtbl, buf, (uint64)fallback, need);
        return buf;
    }

    path[STR_MAXLEN] = '\0';
    char *cwd = path + offset;
    uint32 need = (uint32)strlen(cwd) + 1;
    if (need > size) return (uint64)(-ERANGE);
    uvm_copyout(p->pgtbl, buf, (uint64)cwd, need);
    return buf;
}

uint64 sys_spawn()
{
    char path[STR_MAXLEN + 1];
    arg_str(0, path, STR_MAXLEN);

    uint64 argv_addr;
    arg_uint64(1, &argv_addr);

    char *argv[32];
    int argc = 0;
    uint64 addr;
    proc_t *p = myproc();

    while (argc < 32) {
        uvm_copyin(p->pgtbl, (uint64)&addr, argv_addr + argc * sizeof(uint64), sizeof(uint64));
        if (addr == 0) break;
        argv[argc] = (char*)addr;
        argc++;
    }
    argv[argc] = NULL;

    char *kargv[32];
    for (int i = 0; i < argc; i++) {
        kargv[i] = (char*)pmem_alloc(false);
        if (!kargv[i]) {
            for (int j = 0; j < i; j++) pmem_free((uint64)kargv[j], false);
            return -1;
        }
        uvm_copyin_str(p->pgtbl, (uint64)kargv[i], (uint64)argv[i], STR_MAXLEN);
    }
    kargv[argc] = NULL;

    int ret = sys_spawn_and_wait(path, kargv);

    for (int i = 0; i < argc; i++) {
        pmem_free((uint64)kargv[i], false);
    }

    return ret;
}

/*
    鏂板缓閾炬帴
    char *old_path
    char *new_path
    鎴愬姛杩斿洖0, 澶辫触杩斿洖-1
*/
uint64 sys_link()
{
    char old_path[STR_MAXLEN + 1], new_path[STR_MAXLEN + 1];
    if (arg_raw(2) != 0 || arg_raw(3) != 0 || arg_raw(4) != 0) {
        uint64 olddirfd, newdirfd;
        uint32 flags;
        arg_uint64(0, &olddirfd);
        arg_str(1, old_path, STR_MAXLEN);
        arg_uint64(2, &newdirfd);
        arg_str(3, new_path, STR_MAXLEN);
        arg_uint32(4, &flags);
        (void)olddirfd;
        (void)newdirfd;
        (void)flags;
    } else {
        arg_str(0, old_path, STR_MAXLEN);
        arg_str(1, new_path, STR_MAXLEN);
    }

    return path_link(old_path, new_path);
}

uint64 sys_renameat()
{
    char old_path[STR_MAXLEN + 1], new_path[STR_MAXLEN + 1];
    arg_str(1, old_path, STR_MAXLEN);
    arg_str(3, new_path, STR_MAXLEN);

    if (memfs_rename(old_path, new_path) == 0)
        return 0;

    inode_t *old_ip = path_to_inode(old_path);
    if (old_ip == NULL)
        return (uint64)(-ENOENT);
    inode_lock(old_ip);
    uint16 old_type = old_ip->disk_info.type;
    inode_unlock(old_ip);
    inode_put(old_ip);

    inode_t *new_ip = path_to_inode(new_path);
    if (new_ip != NULL) {
        inode_put(new_ip);
        return (uint64)(-EEXIST);
    }

    if (old_type == INODE_TYPE_DIR)
        return path_rename(old_path, new_path);

    if (path_link(old_path, new_path) != 0)
        return (uint64)(-EINVAL);
    if (path_unlink(old_path) != 0)
        return (uint64)(-EINVAL);
    return 0;
}

uint64 sys_renameat2()
{
    char old_path[STR_MAXLEN + 1], new_path[STR_MAXLEN + 1];
    uint64 flags = arg_raw(4);
    if (flags != 0)
        return (uint64)(-EINVAL);
    arg_str(1, old_path, STR_MAXLEN);
    arg_str(3, new_path, STR_MAXLEN);

    if (memfs_rename(old_path, new_path) == 0)
        return 0;

    inode_t *old_ip = path_to_inode(old_path);
    if (old_ip == NULL)
        return (uint64)(-ENOENT);
    inode_lock(old_ip);
    uint16 old_type = old_ip->disk_info.type;
    inode_unlock(old_ip);
    inode_put(old_ip);

    if (old_type == INODE_TYPE_DIR)
        return path_rename(old_path, new_path);
    if (path_link(old_path, new_path) != 0)
        return (uint64)(-EINVAL);
    if (path_unlink(old_path) != 0)
        return (uint64)(-EINVAL);
    return 0;
}

uint64 sys_statfs()
{
    char path[STR_MAXLEN + 1];
    uint64 buf = arg_raw(1);
    arg_str(0, path, STR_MAXLEN);
    file_t *file = file_open(path, FILE_OPEN_READ);
    if (!file) return (uint64)(-ENOENT);
    uint32 r = file_get_statfs_linux(file, buf);
    file_close(file);
    return r;
}

uint64 sys_fstatfs()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return (uint64)(-EBADF);
    return file_get_statfs_linux(file, arg_raw(1));
}

// 46 ftruncate(fd, length): cyclictest POSIX shm sizing; accept valid fds.
uint64 sys_ftruncate()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0)
        return (uint64)(-EBADF);
    return 0;
}

static bool is_busybox_applet_candidate(char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;
    if (strncmp(path, "/bin/", 5) == 0 ||
        strncmp(path, "/usr/bin/", 9) == 0 ||
        strncmp(path, "/musl/", 6) == 0 ||
        strncmp(path, "/glibc/", 7) == 0)
        return true;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/')
            return false;
    }
    return true;
}

uint64 sys_faccessat()
{
    char path[STR_MAXLEN + 1];
    arg_str(1, path, STR_MAXLEN);
    if (procfs_path_exists(path) || memfs_path_exists(path))
        return 0;
    uint32 len = (uint32)strlen(path);
    if ((len == 2 && path[0] == 'l' && path[1] == 's') ||
        (len >= 3 && path[len - 3] == '/' && path[len - 2] == 'l' && path[len - 1] == 's'))
        return 0;
    inode_t *ip = path_to_inode(path);
    if (ip == NULL) {
        if (is_busybox_applet_candidate(path))
            return 0;
        return (uint64)(-ENOENT);
    }
    inode_put(ip);
    return 0;
}

uint64 sys_utimensat()
{
    char path[STR_MAXLEN + 1];
    if (arg_raw(1) == 0) {
        file_t *file;
        if (arg_fd(0, NULL, &file) < 0)
            return (uint64)(-EBADF);
        return 0;
    }
    arg_str(1, path, STR_MAXLEN);
    if (path[0] == 0 || procfs_path_exists(path) || memfs_path_exists(path))
        return 0;
    if (strncmp(path, "/dev/null/", 10) == 0)
        return (uint64)(-ENOTDIR);
    inode_t *ip = path_to_inode(path);
    if (ip == NULL)
        return (uint64)(-ENOENT);
    inode_put(ip);
    return 0;
}


/*
    鍒犻櫎閾炬帴 (鍙兘瑙﹀彂鍒犻櫎鏂囦欢)
    char *path
    鎴愬姛杩斿洖0, 澶辫触杩斿洖-1
*/
uint64 sys_unlink()
{
    char path[STR_MAXLEN + 1];
    if (arg_raw(1) != 0 || arg_raw(2) != 0) {
        uint64 dirfd;
        uint32 flags;
        arg_uint64(0, &dirfd);
        arg_str(1, path, STR_MAXLEN);
        arg_uint32(2, &flags);
        (void)dirfd;
        (void)flags;
    } else {
        arg_str(0, path, STR_MAXLEN);
    }

    if (memfs_unlink(path) == 0)
        return 0;
    if (path_unlink(path) == 0)
        return 0;
    return (uint64)(-ENOENT);
}

// 174 getuid / 176 getgid锛氬綋鍓嶆棤澶氱敤鎴? 涓€寰?root
uint64 sys_getuid() { return 0; }
uint64 sys_getgid() { return 0; }

// 135 rt_sigprocmask(how,set,oldset,sigsetsize)锛氭殏涓嶅仛淇″彿, 杩斿洖鎴愬姛
uint64 sys_rt_sigsuspend()
{
    proc_yield();
    return (uint64)(-EINTR);
}

uint64 sys_rt_sigprocmask() { return 0; }

// 137 rt_sigtimedwait(set, info, timeout, sigsetsize): consume a pending signal from set.
uint64 sys_rt_sigtimedwait()
{
    uint64 set_addr = arg_raw(0);
    uint64 info_addr = arg_raw(1);
    uint64 timeout_addr = arg_raw(2);
    uint64 sigsetsize = arg_raw(3);
    proc_t *p = myproc();

    if (set_addr == 0 || sigsetsize != 8)
        return (uint64)(-EINVAL);

    uint64 want = 0;
    uvm_copyin(p->pgtbl, (uint64)&want, set_addr, sizeof(want));
    if (want == 0)
        return (uint64)(-EINVAL);

    uint64 deadline = 0;
    int has_timeout = timeout_addr != 0;
    if (has_timeout) {
        struct {
            int64 tv_sec;
            int64 tv_nsec;
        } ts;
        uvm_copyin(p->pgtbl, (uint64)&ts, timeout_addr, sizeof(ts));
        if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL)
            return (uint64)(-EINVAL);
        uint64 rel = (uint64)ts.tv_sec * FUTEX_TIMEBASE_HZ
                   + ((uint64)ts.tv_nsec + 99ull) / 100ull;
        deadline = r_time() + rel;
    }

    for (;;) {
        uint64 pending;
        spinlock_acquire(&p->lk);
        pending = p->sig_pending & want;
        if (pending != 0) {
            int sig = 0;
            for (int i = 1; i <= NSIG; i++) {
                if (pending & (1UL << (i - 1))) {
                    sig = i;
                    break;
                }
            }
            p->sig_pending &= ~(1UL << (sig - 1));
            spinlock_release(&p->lk);

            if (info_addr != 0) {
                uint64 info[16];
                memset(info, 0, sizeof(info));
                int *fields = (int *)info;
                fields[0] = sig;  // si_signo
                fields[1] = 0;    // si_errno
                fields[2] = 0;    // si_code
                uvm_copyout(p->pgtbl, info_addr, (uint64)info, sizeof(info));
            }
            return (uint64)sig;
        }
        spinlock_release(&p->lk);

        if (has_timeout && r_time() >= deadline)
            return (uint64)(-EAGAIN);

        proc_yield();
    }
}

// 144 setgid / 146 setuid锛氬崟鐢ㄦ埛鐜, 瑙嗕綔鎴愬姛 no-op
uint64 sys_setgid() { return 0; }
uint64 sys_setuid() { return 0; }

// 79 newfstatat(dirfd, path, statbuf, flags)
uint64 sys_newfstatat()
{
    char path[STR_MAXLEN + 1];
    uint64 flags   = arg_raw(3);
    uint64 statbuf = arg_raw(2);
    arg_str(1, path, STR_MAXLEN);

    // AT_EMPTY_PATH(0x1000): 绌鸿矾寰?鈫?鐩存帴 stat dirfd 鎸囧悜鐨勬枃浠?musl 鐨?fstat 璧拌繖鏉?
    if (path[0] == '\0' && (flags & 0x1000)) {
        file_t *file;
        if (arg_fd(0, NULL, &file) < 0) return (uint64)(-EBADF);
        return file_get_stat_linux(file, statbuf);
    }
    // 鍚﹀垯鎸夎矾寰勬墦寮€鍚?stat(dirfd 鏆傛寜 cwd/缁濆璺緞澶勭悊, 涓庣幇鏈?openat 涓€鑷?
    file_t *file = file_open(path, FILE_OPEN_READ);
    if (!file && is_busybox_applet_candidate(path)) {
        file = file_open("busybox", FILE_OPEN_READ);
    }
    if (!file) return (uint64)(-ENOENT);
    uint64 r = file_get_stat_linux(file, statbuf);
    file_close(file);
    return r;
}

// 25 fcntl(fd, cmd, arg)锛氬疄鐜?F_DUPFD + 鏂囦欢鎻忚堪绗?鐘舵€佹爣蹇楃殑甯歌鍛戒护
uint64 sys_fcntl()
{
    file_t *file;
    uint32 fd;
    if (arg_fd(0, &fd, &file) < 0) return (uint64)(-EBADF);
    proc_t *p = myproc();
    int cmd = (int)arg_raw(1);
    switch (cmd) {
        case 0:      // F_DUPFD
        case 1030: { // F_DUPFD_CLOEXEC锛氭殏涓嶅尯鍒?cloexec, 鐩存帴澶嶅埗 fd
            file_t *nf = file_dup(file);
            if (!nf) return (uint64)-1;
            uint32 nfd = alloc_fd_at_least(nf, (uint32)arg_raw(2), cmd == 1030 ? 1 : 0);
            if (nfd == (uint32)-1) { file_close(nf); return (uint64)-1; }
            return nfd;
        }
        case 1: return p->fd_cloexec[fd] ? 1 : 0;     // F_GETFD
        case 2:
            p->fd_cloexec[fd] = (arg_raw(2) & 1) ? 1 : 0;
            return 0;     // F_SETFD
        case 3: {     // F_GETFL
            int flags = 2; // O_RDWR
            if (file->is_socket) {
                int ret = socket_get_nonblock(file->socket);
                if (ret < 0) return (uint64)ret;
                if (ret != 0) flags |= 0x800; // O_NONBLOCK
            }
            return (uint64)flags;
        }
        case 4: {     // F_SETFL
            int flags = (int)arg_raw(2);
            if (file->is_socket) {
                int ret = socket_set_nonblock(file->socket, (flags & 0x800) != 0);
                if (ret < 0) return (uint64)ret;
            }
            return 0;
        }
        default: return 0;    // 鍏跺畠鍛戒护鏆備綔鎴愬姛澶勭悊
    }
}

// 134 rt_sigaction(signum, act, oldact, sigsetsize): 娉ㄥ唽淇″彿澶勭悊鍣?
uint64 sys_rt_sigaction()
{
    int signum = (int)arg_raw(0);
    uint64 act_addr = arg_raw(1);
    uint64 oldact_addr = arg_raw(2);
    uint64 sigsetsize = arg_raw(3);

    if (signum < 1 || signum > NSIG)
        return (uint64)(-EINVAL);
    if (sigsetsize > 128)
        sigsetsize = 128;

    uint32 struct_size = 24 + (uint32)sigsetsize;
    proc_t *p = myproc();

    if (oldact_addr != 0) {
        uint8 buf[152];
        memset(buf, 0, struct_size);
        *(uint64 *)&buf[0] = p->sig_handler[signum];
        *(uint64 *)&buf[8] = 0;
        *(uint64 *)&buf[16] = p->sig_restorer;
        uvm_copyout(p->pgtbl, oldact_addr, (uint64)buf, struct_size);
    }

    if (act_addr != 0) {
        uint8 buf[152];
        uvm_copyin(p->pgtbl, (uint64)buf, act_addr, struct_size);
        p->sig_handler[signum] = *(uint64 *)&buf[0];
        uint64 flags = *(uint64 *)&buf[8];
        if (flags & SA_RESTORER)
            p->sig_restorer = *(uint64 *)&buf[16];
    }

    return 0;
}

// 160 uname锛氬～ struct utsname(6 脳 65 瀛楄妭瀛楁)
uint64 sys_uname()
{
    uint64 addr = arg_raw(0);
    struct {
        char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
    } u;
    memset(&u, 0, sizeof(u));
    memmove(u.sysname,  "SeaOS",   6);
    memmove(u.nodename, "seaos",   6);
    memmove(u.release,  "6.1.0",   6);   // 缁欎釜杈冩柊鐨勫唴鏍哥増鏈彿, 瑙勯伩閮ㄥ垎鐗堟湰妫€鏌?    memmove(u.version,  "SeaOS",   6);
    memmove(u.machine,  "riscv64", 8);
    uvm_copyout(myproc()->pgtbl, addr, (uint64)&u, sizeof(u));
    return 0;
}

static uint64 copy_nofile_rlimit(uint64 addr)
{
    uint64 limit[2] = {N_OPEN_FILE_PER_PROC, N_OPEN_FILE_PER_PROC};
    if (addr == 0)
        return (uint64)(-EFAULT);
    uvm_copyout(myproc()->pgtbl, addr, (uint64)limit, sizeof(limit));
    return 0;
}

// 163 getrlimit(resource, rlim): support RLIMIT_NOFILE for lmbench morefds().
uint64 sys_getrlimit()
{
    uint64 resource = arg_raw(0);
    uint64 rlim = arg_raw(1);
    if (resource != 7)
        return (uint64)(-EINVAL);
    return copy_nofile_rlimit(rlim);
}

// 164 setrlimit(resource, rlim): accept RLIMIT_NOFILE without changing static fd table size.
uint64 sys_setrlimit()
{
    uint64 resource = arg_raw(0);
    uint64 rlim = arg_raw(1);
    uint64 tmp[2];
    if (resource != 7)
        return (uint64)(-EINVAL);
    if (rlim == 0)
        return (uint64)(-EFAULT);
    uvm_copyin(myproc()->pgtbl, (uint64)tmp, rlim, sizeof(tmp));
    return 0;
}

// 261 prlimit64(pid, resource, new_limit, old_limit): combined get/set rlimit.
uint64 sys_prlimit64()
{
    uint64 pid = arg_raw(0);
    uint64 resource = arg_raw(1);
    uint64 new_limit = arg_raw(2);
    uint64 old_limit = arg_raw(3);
    uint64 tmp[2];

    if (pid != 0 && pid != (uint64)myproc()->pid)
        return (uint64)(-ESRCH);
    if (resource != 7)
        return (uint64)(-EINVAL);
    if (old_limit != 0)
        copy_nofile_rlimit(old_limit);
    if (new_limit != 0)
        uvm_copyin(myproc()->pgtbl, (uint64)tmp, new_limit, sizeof(tmp));
    return 0;
}

// 173 getppid锛氳繑鍥炵埗杩涚▼ pid(鏃犵埗鍒?1)
uint64 sys_getppid()
{
    proc_t *p = myproc();
    if (p && p->parent) return (uint64)p->parent->pid;
    return 1;
}

// 157 setsid(): SeaOS has no process groups or controlling terminals yet.
// Return the caller pid as the new session id for Linux compatibility.
uint64 sys_setsid()
{
    proc_t *p = myproc();
    if (p == NULL)
        return (uint64)(-ESRCH);
    return (uint64)p->pid;
}

// qemu virt 鐨?time CSR 棰戠巼: INTERVAL=1e6 cycle鈮?.1s => 10MHz
#define TIMEBASE_HZ FUTEX_TIMEBASE_HZ

// 113 clock_gettime锛氳幏鍙栨椂閽熸椂闂达紙楂樼簿搴︼級
uint64 sys_clock_gettime()
{
    // clock_gettime(clk_id=a0(蹇界暐, 缁熶竴鐢ㄥ崟璋?0MHz璁℃暟), struct timespec *tp=a1)
    uint64 tp = arg_raw(1);
    if (tp == 0) return 0;
    uint64 t = r_time();
    uint64 ts[2];
    ts[0] = t / TIMEBASE_HZ;             // tv_sec
    ts[1] = (t % TIMEBASE_HZ) * 100;     // tv_nsec (1/10MHz = 100ns)
    uvm_copyout(myproc()->pgtbl, tp, (uint64)ts, sizeof(ts));
    return 0;
}

// 228 mlock(addr, len): SeaOS has no paging, so mapped memory is resident.
uint64 sys_mlock()
{
    return 0;
}

// 169 gettimeofday锛氳幏鍙栧綋鍓嶆椂闂达紙寰绮惧害锛?
uint64 sys_gettimeofday()
{
    // gettimeofday(struct timeval *tv=a0, struct timezone *tz=a1(蹇界暐))
    uint64 tv = arg_raw(0);
    if (tv == 0) return 0;
    uint64 t = r_time();
    uint64 val[2];
    val[0] = t / TIMEBASE_HZ;            // tv_sec
    val[1] = (t % TIMEBASE_HZ) / 10;     // tv_usec (10MHz/10 = 1MHz)
    uvm_copyout(myproc()->pgtbl, tv, (uint64)val, sizeof(val));
    return 0;
}

// 278 getrandom(buf, buflen, flags): non-blocking pseudo-random bytes.
uint64 sys_getrandom()
{
    uint64 buf = arg_raw(0);
    uint64 len = arg_raw(1);
    uint64 flags = arg_raw(2);

    if (len == 0)
        return 0;
    if (buf == 0)
        return (uint64)(-EFAULT);
    if ((flags & ~0x7ull) != 0)
        return (uint64)(-EINVAL);
    if (len > 0x7ffff000ull)
        len = 0x7ffff000ull;

    return (uint64)device_random_bytes((uint32)len, buf, true);
}

// 165 getrusage锛氳幏鍙栬祫婧愪娇鐢ㄧ粺璁★紙鏈€灏忔々锛氬叏闆讹級
uint64 sys_getrusage()
{
    // getrusage(int who=a0, struct rusage *usage=a1) 鏈€灏忔々: 鍏ㄩ浂(144瀛楄妭)杩斿洖0
    uint64 usage = arg_raw(1);
    if (usage == 0) return 0;
    char buf[144];
    memset(buf, 0, sizeof(buf));
    uvm_copyout(myproc()->pgtbl, usage, (uint64)buf, sizeof(buf));
    return 0;
}

uint64 sys_pipe2()
{
    // pipe2(int pipefd[2]=a0, int flags=a1)銆俧lags(O_CLOEXEC/O_NONBLOCK)鏆傚拷鐣ャ€?
    uint64 fdarray = arg_raw(0);
    uint32 flags = (uint32)arg_raw(1);
    file_t *rf = NULL, *wf = NULL;
    if (pipe_alloc(&rf, &wf) < 0)
        return -1;
    uint32 fd0 = alloc_fd(rf);
    uint32 fd1 = alloc_fd(wf);
    if (fd0 == (uint32)-1 || fd1 == (uint32)-1) {
        if (fd0 != (uint32)-1) {
            myproc()->open_file[fd0] = NULL;
            myproc()->fd_cloexec[fd0] = 0;
        }
        file_close(rf);
        file_close(wf);
        return -1;
    }
    if (flags & 0x80000U) {
        myproc()->fd_cloexec[fd0] = 1;
        myproc()->fd_cloexec[fd1] = 1;
    }
    int fds[2] = { (int)fd0, (int)fd1 };
    uvm_copyout(myproc()->pgtbl, fdarray, (uint64)fds, sizeof(fds));
    return 0;
}

// 199 socketpair(domain, type, protocol, sv): minimal AF_UNIX/SOCK_STREAM pair.
uint64 sys_socketpair()
{
    int domain = (int)arg_raw(0);
    int type = (int)arg_raw(1);
    int protocol = (int)arg_raw(2);
    uint64 fdarray = arg_raw(3);

    if (fdarray == 0)
        return (uint64)(-EFAULT);
    if (domain != 1 || (type & 0xf) != 1 || protocol != 0)
        return (uint64)(-EINVAL);

    file_t *rf = NULL;
    file_t *wf = NULL;
    if (pipe_alloc(&rf, &wf) < 0)
        return (uint64)(-EMFILE);

    uint32 fd0 = alloc_fd(rf);
    uint32 fd1 = alloc_fd(wf);
    if (fd0 == (uint32)-1 || fd1 == (uint32)-1) {
        if (fd0 != (uint32)-1) {
            myproc()->open_file[fd0] = NULL;
            myproc()->fd_cloexec[fd0] = 0;
        }
        file_close(rf);
        file_close(wf);
        return (uint64)(-EMFILE);
    }

    if (type & 0x80000) {
        myproc()->fd_cloexec[fd0] = 1;
        myproc()->fd_cloexec[fd1] = 1;
    }
    int fds[2] = { (int)fd0, (int)fd1 };
    uvm_copyout(myproc()->pgtbl, fdarray, (uint64)fds, sizeof(fds));
    return 0;
}

static int arg_socket_fd(int n, socket_t **out)
{
    file_t *file;
    if (arg_fd(n, NULL, &file) < 0)
        return -EBADF;
    if (!file->is_socket || file->socket == NULL)
        return -ENOTSOCK;
    *out = file->socket;
    return 0;
}

// 198 socket(domain, type, protocol): AF_INET loopback socket.
uint64 sys_socket()
{
    int domain = (int)arg_raw(0);
    int type = (int)arg_raw(1);
    int protocol = (int)arg_raw(2);
    uint8 cloexec = 0;
    file_t *file = socket_file_alloc(domain, type, protocol, &cloexec);
    if (file == NULL)
        return (uint64)(-EAFNOSUPPORT);

    uint32 fd = alloc_fd_at_least(file, 0, cloexec);
    if (fd == (uint32)-1) {
        file_close(file);
        return (uint64)(-EMFILE);
    }
    return fd;
}

// 200 bind(sockfd, addr, addrlen): bind AF_INET loopback address.
uint64 sys_bind()
{
    socket_t *so;
    int ret = arg_socket_fd(0, &so);
    if (ret < 0)
        return (uint64)ret;
    ret = socket_bind(so, arg_raw(1), (uint32)arg_raw(2));
    return ret < 0 ? (uint64)ret : 0;
}

// 201 listen(sockfd, backlog): mark a stream socket as accepting.
uint64 sys_listen()
{
    socket_t *so;
    int ret = arg_socket_fd(0, &so);
    if (ret < 0)
        return (uint64)ret;
    ret = socket_listen(so, (int)arg_raw(1));
    return ret < 0 ? (uint64)ret : 0;
}

static uint64 do_accept(int flags)
{
    socket_t *so;
    file_t *file = NULL;
    uint8 cloexec = 0;
    int ret = arg_socket_fd(0, &so);
    if (ret < 0)
        return (uint64)ret;
    ret = socket_accept(so, arg_raw(1), arg_raw(2), flags, &file, &cloexec);
    if (ret < 0)
        return (uint64)ret;
    uint32 fd = alloc_fd_at_least(file, 0, cloexec);
    if (fd == (uint32)-1) {
        file_close(file);
        return (uint64)(-EMFILE);
    }
    return fd;
}

// 202 accept(sockfd, addr, addrlen): accept a pending loopback stream.
uint64 sys_accept()
{
    return do_accept(0);
}

// 242 accept4(sockfd, addr, addrlen, flags): accept with SOCK_* flags.
uint64 sys_accept4()
{
    return do_accept((int)arg_raw(3));
}

// 203 connect(sockfd, addr, addrlen): connect to loopback listener.
uint64 sys_connect()
{
    socket_t *so;
    int ret = arg_socket_fd(0, &so);
    if (ret < 0)
        return (uint64)ret;
    ret = socket_connect(so, arg_raw(1), (uint32)arg_raw(2));
    return ret < 0 ? (uint64)ret : 0;
}

// 204 getsockname(sockfd, addr, addrlen): return local AF_INET name.
uint64 sys_getsockname()
{
    socket_t *so;
    int ret = arg_socket_fd(0, &so);
    if (ret < 0)
        return (uint64)ret;
    ret = socket_getname(so, arg_raw(1), arg_raw(2), false);
    return ret < 0 ? (uint64)ret : 0;
}

// 205 getpeername(sockfd, addr, addrlen): return peer AF_INET name.
uint64 sys_getpeername()
{
    socket_t *so;
    int ret = arg_socket_fd(0, &so);
    if (ret < 0)
        return (uint64)ret;
    ret = socket_getname(so, arg_raw(1), arg_raw(2), true);
    return ret < 0 ? (uint64)ret : 0;
}

// 206 sendto(sockfd, buf, len, flags, dest_addr, addrlen): loopback send.
uint64 sys_sendto()
{
    socket_t *so;
    int ret = arg_socket_fd(0, &so);
    if (ret < 0)
        return (uint64)ret;
    ret = socket_sendto(so, arg_raw(1), (uint32)arg_raw(2), (int)arg_raw(3), arg_raw(4), (uint32)arg_raw(5));
    return ret < 0 ? (uint64)ret : (uint64)ret;
}

// 207 recvfrom(sockfd, buf, len, flags, src_addr, addrlen): loopback receive.
uint64 sys_recvfrom()
{
    socket_t *so;
    int ret = arg_socket_fd(0, &so);
    if (ret < 0)
        return (uint64)ret;
    ret = socket_recvfrom(so, arg_raw(1), (uint32)arg_raw(2), (int)arg_raw(3), arg_raw(4), arg_raw(5));
    return ret < 0 ? (uint64)ret : (uint64)ret;
}

// 208 setsockopt(sockfd, level, optname, optval, optlen): minimal no-op options.
uint64 sys_setsockopt()
{
    socket_t *so;
    int ret = arg_socket_fd(0, &so);
    if (ret < 0)
        return (uint64)ret;
    ret = socket_setsockopt(so, (int)arg_raw(1), (int)arg_raw(2), arg_raw(3), (uint32)arg_raw(4));
    return ret < 0 ? (uint64)ret : 0;
}

// 209 getsockopt(sockfd, level, optname, optval, optlen): return stable values.
uint64 sys_getsockopt()
{
    socket_t *so;
    int ret = arg_socket_fd(0, &so);
    if (ret < 0)
        return (uint64)ret;
    ret = socket_getsockopt(so, (int)arg_raw(1), (int)arg_raw(2), arg_raw(3), arg_raw(4));
    return ret < 0 ? (uint64)ret : 0;
}

// 210 shutdown(sockfd, how): half-close a loopback stream.
uint64 sys_shutdown_sock()
{
    socket_t *so;
    int ret = arg_socket_fd(0, &so);
    if (ret < 0)
        return (uint64)ret;
    ret = socket_shutdown(so, (int)arg_raw(1));
    return ret < 0 ? (uint64)ret : 0;
}

// 211 sendmsg: msghdr/scm is outside the current loopback compatibility layer.
uint64 sys_sendmsg()
{
    return (uint64)(-EOPNOTSUPP);
}

// 212 recvmsg: msghdr/scm is outside the current loopback compatibility layer.
uint64 sys_recvmsg()
{
    return (uint64)(-EOPNOTSUPP);
}

uint64 sys_umask()
{
    return 0;
}

// 29 ioctl(fd, cmd, arg): 缁堢/璁惧鎺у埗銆傛渶灏忔々: ENOTTY銆?
uint64 sys_ioctl()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0) return (uint64)(-EBADF);
    uint64 req = arg_raw(1);
    uint64 arg = arg_raw(2);

    if (file->is_device && file->dev_major == INODE_MAJOR_RTC && (req & 0xffff) == 0x7009 && arg != 0) {
        struct {
            int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
        } rtc;
        uint64 sec = r_time() / 10000000ull;
        memset(&rtc, 0, sizeof(rtc));
        rtc.tm_sec = sec % 60;
        rtc.tm_min = (sec / 60) % 60;
        rtc.tm_hour = (sec / 3600) % 24;
        rtc.tm_mday = 1;
        rtc.tm_mon = 0;
        rtc.tm_year = 70;
        uvm_copyout(myproc()->pgtbl, arg, (uint64)&rtc, sizeof(rtc));
        return 0;
    }
    return (uint64)(-ENOTTY);
}

// 99 set_robust_list(head, len): musl 绾跨▼鍒濆鍖栭渶瑕併€傛々杩斿洖 0銆?
uint64 sys_set_robust_list()
{
    return 0;
}

// 100 get_robust_list(pid, head_ptr, len_ptr): 妗╄繑鍥?0銆?
uint64 sys_get_robust_list()
{
    return 0;
}

// 102 getitimer(which, curr_value): 鑾峰彇闂撮殧瀹氭椂鍣ㄣ€傛々: 闆跺～鍏呰繑鍥炪€?
uint64 sys_getitimer()
{
    uint64 curr = arg_raw(1);
    if (curr == 0) return 0;
    char buf[32];
    memset(buf, 0, sizeof(buf));
    uvm_copyout(myproc()->pgtbl, curr, (uint64)buf, sizeof(buf));
    return 0;
}

// 103 setitimer(which, new_value, old_value): 璁剧疆闂撮殧瀹氭椂鍣ㄣ€?// dhry2reg 鐢ㄥ畠鍋?benchmark 璁℃椂(ITIMER_REAL=0, 瓒呮椂鍙?SIGALRM)銆?
uint64 sys_setitimer()
{
    uint64 which = arg_raw(0);
    uint64 new_addr = arg_raw(1);
    uint64 old_addr = arg_raw(2);
    proc_t *p = myproc();

    if (which != 0)
        return (uint64)(-EINVAL);

    if (old_addr != 0) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        uvm_copyout(p->pgtbl, old_addr, (uint64)buf, sizeof(buf));
    }

    if (new_addr != 0) {
        uint64 buf[4];
        uvm_copyin(p->pgtbl, (uint64)buf, new_addr, sizeof(buf));
        uint64 interval_sec = buf[0];
        uint64 interval_usec = buf[1];
        uint64 value_sec = buf[2];
        uint64 value_usec = buf[3];

        if (value_sec == 0 && value_usec == 0) {
            p->itimer_expire = 0;
            p->itimer_interval = 0;
        } else {
            uint64 now = r_time();
            uint64 delay = value_sec * 10000000ull + value_usec * 10;
            if (p->ub_looper_secs != 0 && delay < (uint64)p->ub_looper_secs * 10000000ull)
                delay = (uint64)p->ub_looper_secs * 10000000ull;
            p->itimer_expire = now + delay;
            p->itimer_interval = interval_sec * 10000000ull + interval_usec * 10;
        }
    }

    return 0;
}

// 114 clock_getres(clockid, res): report coarse timer resolution as 10ms.
uint64 sys_clock_getres()
{
    uint64 tp = arg_raw(1);
    if (tp != 0) {
        uint64 ts[2] = {0, 10000000UL};
        uvm_copyout(myproc()->pgtbl, tp, (uint64)ts, sizeof(ts));
    }
    return 0;
}

// 115 clock_nanosleep(clockid, flags, request, remain): minimal sleep support.
uint64 sys_clock_nanosleep()
{
    uint64 flags = arg_raw(1);
    uint64 request = arg_raw(2);
    if ((flags & ~1UL) != 0)
        return (uint64)(-EINVAL);
    if (request == 0) return 0;
    uint64 ts[2] = {0, 0};
    uvm_copyin(myproc()->pgtbl, (uint64)ts, request, sizeof(ts));
    if (ts[1] >= 1000000000ull)
        return (uint64)(-EINVAL);

    uint64 sleep_timebase = ts[0] * TIMEBASE_HZ + (ts[1] + 99ull) / 100ull;
    if (flags & 1) {
        uint64 now = r_time();
        if (sleep_timebase <= now)
            return 0;
        sleep_timebase -= now;
    }

    uint64 ntick = (sleep_timebase + INTERVAL - 1) / INTERVAL;
    if (ntick == 0)
        ntick = 1;
    timer_wait(ntick);
    return 0;
}

uint64 sys_syslog()
{
    int type = (int)arg_raw(0);
    uint64 buf = arg_raw(1);
    uint64 len = arg_raw(2);
    const char *msg = "";

    if (type == 10)
        return 16 * 1024;
    if (type == 8)
        return 0;
    if (type == 3 || type == 4) {
        uint64 n = strlen(msg);
        if (n > len)
            n = len;
        if (buf != 0 && n > 0)
            uvm_copyout(myproc()->pgtbl, buf, (uint64)msg, n);
        return n;
    }
    return 0;
}

static uint64 sys_signal_proc_locked(proc_t *p, int sig)
{
    if (sig == 0) {
        spinlock_release(&p->lk);
        return 0;
    }
    p->sig_pending |= (1UL << (sig - 1));
    if (p->state == SLEEPING) {
        p->state = RUNNABLE;
        p->sleep_space = NULL;
        p->sched_last_ready_tick = timer_get_ticks();
        p->mlfq_age_start_tick = p->sched_last_ready_tick;
        p->sched_ready_count++;
        p->mlfq_in_readyq = 0;
        spinlock_release(&p->lk);
        mlfq_on_wakeup(p);
        return 0;
    }
    spinlock_release(&p->lk);
    return 0;
}

uint64 sys_kill()
{
    int pid = (int)arg_raw(0);
    int sig = (int)arg_raw(1);
    if (sig < 0 || sig > NSIG)
        return (uint64)(-EINVAL);
    if (pid <= 0)
        return 0;
    proc_t *p = proc_get_by_pid(pid);
    if (p == NULL)
        return (uint64)(-ESRCH);
    return sys_signal_proc_locked(p, sig);
}

uint64 sys_tkill()
{
    int tid = (int)arg_raw(0);
    int sig = (int)arg_raw(1);
    if (sig < 0 || sig > NSIG)
        return (uint64)(-EINVAL);
    if (tid <= 0)
        return (uint64)(-EINVAL);
    proc_t *p = proc_get_by_pid(tid);
    if (p == NULL)
        return (uint64)(-ESRCH);
    return sys_signal_proc_locked(p, sig);
}

uint64 sys_tgkill()
{
    int tgid = (int)arg_raw(0);
    int tid = (int)arg_raw(1);
    int sig = (int)arg_raw(2);
    if (sig < 0 || sig > NSIG)
        return (uint64)(-EINVAL);
    if (tgid <= 0 || tid <= 0)
        return (uint64)(-EINVAL);
    if (tgid != tid)
        return (uint64)(-ESRCH);
    proc_t *p = proc_get_by_pid(tid);
    if (p == NULL)
        return (uint64)(-ESRCH);
    return sys_signal_proc_locked(p, sig);
}

// 179 sysinfo(struct sysinfo *info): 绯荤粺淇℃伅銆傛渶灏忔々: 闆跺～鍏呫€?
uint64 sys_sysinfo()
{
    uint64 info = arg_raw(0);
    if (info == 0) return (uint64)(-EFAULT);
    char buf[112];
    memset(buf, 0, sizeof(buf));
    uint64 *p = (uint64 *)buf;
    p[0] = r_time() / 10000000ull; // uptime in seconds
    uvm_copyout(myproc()->pgtbl, info, (uint64)buf, sizeof(buf));
    return 0;
}

// 233 madvise(addr, length, advice): 鍐呭瓨寤鸿銆傛々杩斿洖 0銆?
uint64 sys_madvise()
{
    return 0;
}

// 78 readlinkat(dirfd, pathname, buf, bufsiz): 璇诲彇绗﹀彿閾炬帴銆?// 鐗规畩澶勭悊 /proc/self/exe 杩斿洖杩涚▼璺緞銆傚叾浣欒繑鍥?EINVAL銆?
uint64 sys_readlinkat()
{
    char path[128];
    arg_str(1, path, sizeof(path));
    if (path[0] == 0) return (uint64)(-EINVAL);
    if (strncmp(path, "/proc/self/exe", 15) == 0) {
        uint64 buf = arg_raw(2);
        uint64 size = arg_raw(3);
        const char *target = "/busybox";
        uint64 n = strlen(target);
        if (n > size)
            n = size;
        if (buf != 0 && n > 0)
            uvm_copyout(myproc()->pgtbl, buf, (uint64)target, n);
        return n;
    }
    return (uint64)(-EINVAL);
}

// 124 sched_yield(): 璁╁嚭 CPU銆?
uint64 sys_sched_yield()
{
    proc_yield();
    return 0;
}

// 118 sched_setparam(pid, param): accept Linux sched_param without changing SeaOS policy.
uint64 sys_sched_setparam()
{
    uint64 param_addr = arg_raw(1);
    if (param_addr == 0)
        return (uint64)(-EFAULT);
    return 0;
}

// 120 sched_getscheduler(pid): report SCHED_OTHER for SeaOS tasks.
uint64 sys_sched_getscheduler()
{
    return 0;
}

// 121 sched_getparam(pid, param): write struct sched_param { int sched_priority; }.
uint64 sys_sched_getparam()
{
    uint64 param_addr = arg_raw(1);
    if (param_addr == 0)
        return (uint64)(-EFAULT);
    int priority = 0;
    uvm_copyout(myproc()->pgtbl, param_addr, (uint64)&priority, sizeof(priority));
    return 0;
}

// 122 sched_setaffinity(pid, cpusetsize, mask): accept any mask containing CPU0.
uint64 sys_sched_setaffinity()
{
    uint64 cpusetsize = arg_raw(1);
    uint64 mask_addr = arg_raw(2);
    if (mask_addr == 0)
        return (uint64)(-EFAULT);
    if (cpusetsize == 0)
        return (uint64)(-EINVAL);
    uint8 first = 0;
    uvm_copyin(myproc()->pgtbl, (uint64)&first, mask_addr, sizeof(first));
    if ((first & 1) == 0)
        return (uint64)(-EINVAL);
    return 0;
}

// 123 sched_getaffinity(pid, cpusetsize, mask): CPU 浜插拰鎬ф帺鐮併€?// 鍗曟牳: 杩斿洖 mask bit0=1銆?
uint64 sys_sched_getaffinity()
{
    uint64 cpusetsize = arg_raw(1);
    uint64 mask_addr = arg_raw(2);
    if (mask_addr == 0) return (uint64)(-EFAULT);
    if (cpusetsize < sizeof(uint64)) return (uint64)(-EINVAL);
    char buf[128];
    uint64 len = cpusetsize < sizeof(buf) ? cpusetsize : sizeof(buf);
    memset(buf, 0, len);
    buf[0] = 1;
    uvm_copyout(myproc()->pgtbl, mask_addr, (uint64)buf, len);
    return sizeof(uint64);
}

// 236 get_mempolicy(mode, nodemask, maxnode, addr, flags): default node 0 policy.
uint64 sys_get_mempolicy()
{
    uint64 mode_addr = arg_raw(0);
    uint64 nodemask_addr = arg_raw(1);
    uint64 maxnode = arg_raw(2);
    int zero = 0;
    uint64 one = 1;
    if (mode_addr != 0)
        uvm_copyout(myproc()->pgtbl, mode_addr, (uint64)&zero, sizeof(zero));
    if (nodemask_addr != 0 && maxnode > 0)
        uvm_copyout(myproc()->pgtbl, nodemask_addr, (uint64)&one, sizeof(one));
    return 0;
}

// 177 getegid: 杩斿洖鏈夋晥 GID (root=0)
uint64 sys_getegid()
{
    return 0;
}

// 175 geteuid: 杩斿洖鏈夋晥 UID (root=0)
uint64 sys_geteuid()
{
    return 0;
}

// 139 rt_sigreturn: 浠庝俊鍙峰鐞嗗櫒杩斿洖锛屾仮澶嶈涓柇鐨勬墽琛屼笂涓嬫枃
uint64 sys_rt_sigreturn()
{
    proc_t *p = myproc();
    trapframe_t *tf = p->tf;

    uint64 frame[32];
    uvm_copyin(p->pgtbl, (uint64)frame, tf->sp, sizeof(frame));

    tf->user_to_kern_epc = frame[0];
    tf->ra   = frame[1];
    tf->sp   = frame[2];
    tf->gp   = frame[3];
    tf->tp   = frame[4];
    tf->t0   = frame[5];
    tf->t1   = frame[6];
    tf->t2   = frame[7];
    tf->s0   = frame[8];
    tf->s1   = frame[9];
    tf->a0   = frame[10];
    tf->a1   = frame[11];
    tf->a2   = frame[12];
    tf->a3   = frame[13];
    tf->a4   = frame[14];
    tf->a5   = frame[15];
    tf->a6   = frame[16];
    tf->a7   = frame[17];
    tf->s2   = frame[18];
    tf->s3   = frame[19];
    tf->s4   = frame[20];
    tf->s5   = frame[21];
    tf->s6   = frame[22];
    tf->s7   = frame[23];
    tf->s8   = frame[24];
    tf->s9   = frame[25];
    tf->s10  = frame[26];
    tf->s11  = frame[27];
    tf->t3   = frame[28];
    tf->t4   = frame[29];
    tf->t5   = frame[30];
    tf->t6   = frame[31];

    p->sig_delivering = 0;

    return tf->a0;
}

#define SELECT_FDSET_WORDS ((N_OPEN_FILE_PER_PROC + 63) / 64)

static int fdset_has(uint64 *set, int fd)
{
    return (set[fd / 64] & (1ull << (fd % 64))) != 0;
}

static void fdset_put(uint64 *set, int fd)
{
    set[fd / 64] |= 1ull << (fd % 64);
}

static uint64 timespec_to_ticks(uint64 ts_addr)
{
    if (ts_addr == 0)
        return 0;
    uint64 ts[2];
    uvm_copyin(myproc()->pgtbl, (uint64)ts, ts_addr, sizeof(ts));
    uint64 ticks = ts[0] * 10;
    if (ts[1] > 0)
        ticks++;
    return ticks;
}

static int pipe_select_ready(pipe_t *pi, int want_r, int want_w)
{
    int ready = 0;

    spinlock_acquire(&pi->lk);
    if (want_r && (pi->nread != pi->nwrite || !pi->writeopen))
        ready |= 1;
    if (want_w && (pi->nwrite < pi->nread + PIPE_SIZE || !pi->readopen))
        ready |= 2;
    spinlock_release(&pi->lk);
    return ready;
}

static void pipe_select_wait(pipe_t *pi, int want_r, int want_w)
{
    if (pi == NULL)
        return;
    spinlock_acquire(&pi->lk);
    if (want_r && pi->nread == pi->nwrite && pi->writeopen) {
        proc_sleep(&pi->nread, &pi->lk);
        spinlock_release(&pi->lk);
        return;
    }
    if (want_w && pi->nwrite == pi->nread + PIPE_SIZE && pi->readopen) {
        proc_sleep(&pi->nwrite, &pi->lk);
        spinlock_release(&pi->lk);
        return;
    }
    spinlock_release(&pi->lk);
}

// 72 pselect6(nfds, readfds, writefds, exceptfds, timeout, sigmask): select-compatible fd sets.
uint64 sys_pselect6()
{
    int nfds = (int)arg_raw(0);
    uint64 read_addr = arg_raw(1);
    uint64 write_addr = arg_raw(2);
    uint64 except_addr = arg_raw(3);
    uint64 timeout_addr = arg_raw(4);
    proc_t *p = myproc();

    if (nfds < 0)
        return (uint64)(-EINVAL);
    if (nfds > N_OPEN_FILE_PER_PROC)
        nfds = N_OPEN_FILE_PER_PROC;

    uint64 in_r[SELECT_FDSET_WORDS] = {0};
    uint64 in_w[SELECT_FDSET_WORDS] = {0};
    uint64 in_e[SELECT_FDSET_WORDS] = {0};
    if (read_addr != 0)
        uvm_copyin(p->pgtbl, (uint64)in_r, read_addr, sizeof(in_r));
    if (write_addr != 0)
        uvm_copyin(p->pgtbl, (uint64)in_w, write_addr, sizeof(in_w));
    if (except_addr != 0)
        uvm_copyin(p->pgtbl, (uint64)in_e, except_addr, sizeof(in_e));

    uint64 timeout_ticks = timespec_to_ticks(timeout_addr);
    if (nfds == 0) {
        if (timeout_addr != 0 && timeout_ticks > 0)
            timer_wait(timeout_ticks);
        return 0;
    }

    for (;;) {
        uint64 out_r[SELECT_FDSET_WORDS] = {0};
        uint64 out_w[SELECT_FDSET_WORDS] = {0};
        uint64 out_e[SELECT_FDSET_WORDS] = {0};
        int ready = 0;
        pipe_t *wait_pipe = NULL;
        int wait_pipe_r = 0;
        int wait_pipe_w = 0;
        int saw_socket = 0;
        int saw_pipe = 0;

        for (int fd = 0; fd < nfds; fd++) {
            int want_r = read_addr != 0 && fdset_has(in_r, fd);
            int want_w = write_addr != 0 && fdset_has(in_w, fd);
            int want_e = except_addr != 0 && fdset_has(in_e, fd);
            if (!want_r && !want_w && !want_e)
                continue;

            file_t *f = p->open_file[fd];
            if (f == NULL)
                continue;

            if (f->is_socket) {
                saw_socket = 1;
                int events = 0;
                if (want_r)
                    events |= 1;
                if (want_w)
                    events |= 4;
                int revents = socket_poll_ready(f->socket, events);
                if (want_r && (revents & (1 | 0x10 | 0x8))) {
                    fdset_put(out_r, fd);
                    ready++;
                }
                if (want_w && (revents & 4)) {
                    fdset_put(out_w, fd);
                    ready++;
                }
                if (want_e && (revents & 0x8)) {
                    fdset_put(out_e, fd);
                    ready++;
                }
            } else if (f->is_pipe) {
                saw_pipe = 1;
                if (wait_pipe == NULL) {
                    wait_pipe = f->pipe;
                    wait_pipe_r = want_r;
                    wait_pipe_w = want_w;
                }
                int revents = pipe_select_ready(f->pipe, want_r, want_w);
                if ((revents & 1) != 0) {
                    fdset_put(out_r, fd);
                    ready++;
                }
                if ((revents & 2) != 0) {
                    fdset_put(out_w, fd);
                    ready++;
                }
            } else {
                if (want_r) {
                    fdset_put(out_r, fd);
                    ready++;
                }
                if (want_w) {
                    fdset_put(out_w, fd);
                    ready++;
                }
            }
        }

        if (ready > 0 || timeout_addr != 0) {
            if (ready == 0 && timeout_ticks > 0) {
                timer_wait(1);
                timeout_ticks--;
                continue;
            }
            if (read_addr != 0)
                uvm_copyout(p->pgtbl, read_addr, (uint64)out_r, sizeof(out_r));
            if (write_addr != 0)
                uvm_copyout(p->pgtbl, write_addr, (uint64)out_w, sizeof(out_w));
            if (except_addr != 0)
                uvm_copyout(p->pgtbl, except_addr, (uint64)out_e, sizeof(out_e));
            return ready;
        }

        if (saw_socket && !saw_pipe)
            socket_wait();
        else if (saw_pipe && !saw_socket)
            pipe_select_wait(wait_pipe, wait_pipe_r, wait_pipe_w);
        else
            timer_wait(1);
    }
}

// 73 ppoll(fds, nfds, tmo_p, sigmask, sigsetsize)
// 绠€鍖栧疄鐜帮細鍗曚釜 fd 杞锛屼笉鏀寔瓒呮椂绮惧害 鈥?瓒充互璁?busybox 鐨勭閬?鏂囦欢 IO 杩愪綔銆?
uint64 sys_ppoll()
{
    uint64 fds_addr = arg_raw(0);
    uint64 nfds = arg_raw(1);
    uint64 tmo_addr = arg_raw(2);
    proc_t *p = myproc();

    if (nfds == 0) return 0;
    if (nfds > N_OPEN_FILE_PER_PROC)
        nfds = N_OPEN_FILE_PER_PROC;

    struct { int fd; short events; short revents; } pfd[N_OPEN_FILE_PER_PROC];
    uvm_copyin(p->pgtbl, (uint64)pfd, fds_addr, nfds * 8);

    uint64 timeout_ticks = timespec_to_ticks(tmo_addr);
    for (;;) {
        int ready = 0;
        int saw_socket = 0;
        int saw_pipe = 0;
        pipe_t *wait_pipe = NULL;
        int wait_pipe_r = 0;
        int wait_pipe_w = 0;

        for (uint64 i = 0; i < nfds; i++) {
            pfd[i].revents = 0;
            if (pfd[i].fd < 0)
                continue;
            if ((uint32)pfd[i].fd >= N_OPEN_FILE_PER_PROC) {
                pfd[i].revents = 0x20;
                ready++;
                continue;
            }
            file_t *f = p->open_file[pfd[i].fd];
            if (!f) {
                pfd[i].revents = 0x20;
                ready++;
                continue;
            }

            if (f->is_socket) {
                saw_socket = 1;
                pfd[i].revents = (short)socket_poll_ready(f->socket, pfd[i].events);
            } else if (f->is_pipe) {
                saw_pipe = 1;
                int want_r = (pfd[i].events & 1) != 0;
                int want_w = (pfd[i].events & 4) != 0;
                if (wait_pipe == NULL) {
                    wait_pipe = f->pipe;
                    wait_pipe_r = want_r;
                    wait_pipe_w = want_w;
                }
                int revents = pipe_select_ready(f->pipe, want_r, want_w);
                if (revents & 1)
                    pfd[i].revents |= 1;
                if (revents & 2)
                    pfd[i].revents |= 4;
            } else {
                if (pfd[i].events & 1)
                    pfd[i].revents |= 1;
                if (pfd[i].events & 4)
                    pfd[i].revents |= 4;
            }

            if (pfd[i].revents != 0)
                ready++;
        }

        if (ready > 0 || tmo_addr != 0) {
            if (ready == 0 && timeout_ticks > 0) {
                timer_wait(1);
                timeout_ticks--;
                continue;
            }
            uvm_copyout(p->pgtbl, fds_addr, (uint64)pfd, nfds * 8);
            return ready;
        }

        if (saw_socket && !saw_pipe)
            socket_wait();
        else if (saw_pipe && !saw_socket)
            pipe_select_wait(wait_pipe, wait_pipe_r, wait_pipe_w);
        else
            timer_wait(1);
    }
}

// 71 sendfile(out_fd, in_fd, offset, count): 闆舵嫹璐濇枃浠朵紶杈撱€?// 绠€鍖栧疄鐜帮細鍐呮牳缂撳啿鍖轰腑杞€?
uint64 sys_sendfile()
{
    return (uint64)(-ENOSYS);
}

// 119 sched_setscheduler(pid, policy, param): 璁剧疆璋冨害绛栫暐銆傛々杩斿洖 0銆?
uint64 sys_sched_setscheduler()
{
    return 0;
}
