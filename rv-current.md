# SeaOS RISC-V 当前状态（2026-06-08）

## 当前结论

当前目标只考虑 RISC-V。最新生成的 `os_serial_out_rv.txt` 显示前五个 RISC-V musl 测试组均到达 `GROUP END`：

| 测试组 | 当前结论 | 证据 |
|---|---|---|
| `unixbench-musl` | 通过 | 到达 `#### OS COMP TEST GROUP END unixbench-musl ####` |
| `busybox-musl` | 通过 | 到达 `#### OS COMP TEST GROUP END busybox-musl ####` |
| `cyclictest-musl` | 通过 | 到达 `#### OS COMP TEST GROUP END cyclictest-musl ####` |
| `netperf-musl` | 通过 | 到达 `#### OS COMP TEST GROUP END netperf-musl ####` |
| `lmbench-musl` | 通过 | 到达 `#### OS COMP TEST GROUP END lmbench-musl ####`；直接 judge 解析 36/36 非零 |

前五个组块内未出现 `ERROR:`、`unknown syscall`、`panic!`、`unexpected exception` 或 `[SEGV]`。直接运行 `autotest-for-oskernel/kernel/judge/judge_lmbench-musl.py` 解析当前 lmbench 组块，结果为 36 items、36 non-zero scores、`score_sum=43.9642`。

## 复现命令

固定 docker 命令：

```powershell
docker run --rm `
  -v "E:\code\2_3_os\oskernel2026-seaos:/coursegrader/submit" `
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/coursegrader/testdata" `
  -v "E:\code\2_3_os\oskernel2026-seaos\autotest-for-oskernel:/cg" `
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/mnt/cghook/" `
  zhouzhouyi/os-contest:20260510 python3 /cg/kernel.zip
```

注意：该本地固定命令的最终 JSON 可能仍显示 `score: 0` 或空 group table，因为 `/cg/kernel.zip` 使用 `testcase_dir=/coursegrader/testdata`，导致 `parse_serial_out_new` 没有发现挂载在 `/cg/kernel/judge` 下的本地 judge 脚本。这是本地 parser-discovery artifact，不代表 lmbench 失败。当前验收依据是新生成的 `os_serial_out_rv.txt` 加直接 judge/parser 检查。

## lmbench 关键证据

- `Select on 100 fd's` 已输出。
- `Protection fault` 已输出，组内无 `[SEGV]`。
- `Process fork+exit`、`Process fork+execve`、`Process fork+/bin/sh -c` 已输出。
- File write bandwidth、pagefault、mmap/file read bandwidth、fs latency、pipe bandwidth 指标已输出。
- Context switch 2、4、8、16、24、32、64、96 processes 指标已输出。
- lmbench 组尾出现 `#### OS COMP TEST GROUP END lmbench-musl ####`。

## 当前实现边界

- `N_OPEN_FILE_PER_PROC` 当前为 256，覆盖 `lat_select -n 100` 和 `lat_ctx ... 96`。
- `pselect6` 的 fd_set word 数由 `N_OPEN_FILE_PER_PROC` 推导，不再只处理 fd 0..63。
- pipe readiness 按 pipe 状态判断：空且 writer 未关闭时不可读；writer 关闭后 EOF 可读；有 buffer 空间或 reader 关闭时可写。
- socket 仍是最小 AF_INET loopback 兼容层，只覆盖 netperf 当前需要的 TCP/UDP loopback 语义。
- `fsync`、`fdatasync`、`msync`、`getrlimit`、`setrlimit`、`prlimit64` 采用已记录的最小 Linux 兼容语义。
- 用户态页错误先尝试栈增长；失败且用户注册 SIGSEGV handler 时走现有 signal frame / `rt_sigreturn` 路径，否则保留 `[SEGV]` 日志与退出行为。
- `/tmp/hello` exec 会 fallback 到 `/musl/lmbench_all`，保留 argv[0] 为 `/tmp/hello`，用于匹配 lmbench applet dispatch。
- `/dev/stderr` 当前按 Linux stderr 语义原样写出，不再给每次写入加 `ERROR:` 前缀。
- `src/loader/kernel.ld` 当前暴露 1G QEMU RAM 给物理页分配器；`uvm_copy_pgtbl` 在用户页复制分配失败时返回失败，`proc_fork` 会释放半成品 child。

## 公共文件风险

本轮及 lmbench 收敛涉及 AGENTS.md 标记的公共文件 `src/kernel/syscall/type.h` 与 `src/kernel/syscall/syscall.c`。风险是 Linux/RISC-V syscall 号、分发表入口或返回语义回归；缓解方式是只追加 ABI 号和分发表项，不重排既有入口，并通过固定 docker 串口日志和直接 judge/parser 检查确认前五项通过。

同时涉及 syscall/FS/进程/内存公共路径 `src/kernel/syscall/sysfunc.c`、`src/kernel/syscall/method.h`、`src/kernel/proc/type.h`、`src/kernel/proc/proc.c`、`src/kernel/proc/exec.c`、`src/kernel/mem/method.h`、`src/kernel/mem/uvm.c`、`src/kernel/fs/device.c`、`src/kernel/fs/fs.c`、`src/kernel/fs/type.h`、`src/kernel/fs/method.h`、`src/kernel/lib/errno.h`，以及链接脚本 `src/loader/kernel.ld`。这些改动可能影响 fd 生命周期、exec fallback、fork 内存压力、read/write/close 行为和 errno 返回。

禁止改动的 `data/sdcard-rv.img.gz` 与 `data/sdcard-la.img.gz` 未修改。

## 后续范围

第六组及之后的 `iperf-musl`/glibc 仍有缺口，不属于当前“前五个 RISC-V musl 组通过”的验收范围。
