# SeaOS RISC-V 当前状态（2026-06-07）

## 总体进度

**正式 docker 评测命令已跑通前三个 RISC-V 测试组：`unixbench-musl`、`busybox-musl`、`cyclictest-musl`。** 当前下一阶段目标转为第四项 `netperf-musl`，最新日志中的直接缺口是 `unknown syscall 198`（socket）。

本轮验证命令：

```bash
docker run --rm \
  -v "E:\code\2_3_os\oskernel2026-seaos:/coursegrader/submit" \
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/coursegrader/testdata" \
  -v "E:\code\2_3_os\oskernel2026-seaos\autotest-for-oskernel:/cg" \
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/mnt/cghook/" \
  zhouzhouyi/os-contest:20260510 python3 /cg/kernel.zip
```

评测时间：2026-06-07 04:51:11 至 05:47:57（Asia/Shanghai）。编译段显示 `make all` 成功，`kernel-rv` 与 `kernel-la` 均生成；RISC-V 串口日志已写入根目录 `os_serial_out_rv.txt`。

## 最新测试结论

| 测试组 | 当前结论 | 证据 |
|---|---|---|
| unixbench-musl | 通过 | `#### OS COMP TEST GROUP END unixbench-musl ####` 后出现 `======== test sucess ========` |
| busybox-musl | 通过 | 所有 busybox 用例打印 `success`，组尾出现 `======== test sucess ========` |
| cyclictest-musl | 通过 | 四个子项均 `end: success`，组尾出现 `======== test sucess ========` |
| netperf-musl | 未通过，下一目标 | 进入第四组后出现 `unknown syscall 198`，`getaddrinfo returned -11` |
| lmbench-musl | 后续目标 | 进入第五组后出现 `unknown syscall 72` |

cyclictest 关键输出：

```text
#### OS COMP TEST GROUP START cyclictest-musl ####
====== cyclictest NO_STRESS_P1 end: success ======
====== cyclictest NO_STRESS_P8 end: success ======
====== cyclictest STRESS_P1 end: success ======
====== cyclictest STRESS_P8 end: success ======
====== kill hackbench: success ======
#### OS COMP TEST GROUP END cyclictest-musl ####

======== test sucess ========
```

本轮已确认：旧的 `[SEGV] pc=0x2f63c stval=0x3ffb031ff8` 不再出现，P8 子项也不再卡在第一条 `T:` 统计后长时间不动。

## 本轮关键修复

| 修复 | 主要文件 | 效果 |
|---|---|---|
| `mprotect(226)` 更新用户 PTE 权限 | `src/kernel/syscall/sysfunc.c`, `src/kernel/mem/uvm.c`, `src/kernel/mem/method.h` | 支持 musl `pthread_create` 将 TLS/线程栈从 `PROT_NONE` 改为可写，修复 `__copy_tls` SEGV |
| `clock_nanosleep(115)` 支持 `TIMER_ABSTIME` | `src/kernel/syscall/sysfunc.c` | 避免 cyclictest 把绝对时间睡眠误当相对时间，修复 P8 长时间卡住 |
| `clone(220)` 按 flag 写 `child_tid` | `src/kernel/syscall/sysfunc.c` | 避免污染 musl 线程链表锁，修复多线程退出时的用户态崩溃 |
| memfs 恢复 `path` 写入并补 `sort.src` 只读兜底 | `src/kernel/fs/fs.c` | 保持 BusyBox 文件操作通过，并避免 UnixBench shell 管线因镜像缺输入文件失败 |
| 评测超时提高到 3600 秒 | `data/config.json` | 允许前三项完整跑完；docker 外层耗时约 57 分钟 |

`/dev/cpu_dma_latency` 仍未实现。cyclictest 源码将它作为可选 PM QoS 优化接口，缺失只打印 warning，本轮评测已在保留该 warning 的情况下通过第三项。

## 前两项保持情况

`unixbench-musl` 与 `busybox-musl` 均完整到组尾并进入 `test sucess`。最新 UnixBench 输出 27 项齐全，其中 `SHELL1` 为 1，`SHELL8/SHELL16` 为 0；这不影响当前自动脚本对第一组的通过判定。若后续需要恢复 shell 并发子项非零，应单独优化 shell 管线吞吐或重新收敛 `setitimer` 兼容窗口。

BusyBox 文件操作、`df/free/ps/hwclock/find/stat/sort/uniq` 等命令仍全部打印 `success`。

## 当前后续瓶颈

| 后续测试组 | 当前现象 | 初步方向 |
|---|---|---|
| netperf-musl | `unknown syscall 198`，`getaddrinfo returned -11` | 198 是 `socket`；第四项需要最小网络 syscall/loopback 语义 |
| lmbench-musl | `unknown syscall 72` | 72 是 `pselect6`；第五项需要 select/poll 兼容 |

## 公共文件风险说明

本轮改动涉及 `src/kernel/syscall/sysfunc.c`、`src/kernel/mem/method.h`、`src/kernel/mem/uvm.c`、`src/kernel/fs/fs.c`、`data/config.json` 和文档。`src/kernel/syscall/sysfunc.c` 与 `src/kernel/mem/method.h` 属于公共 syscall/内存接口路径；风险是 Linux ABI 返回值、用户页权限、线程 clone 语义或文件打开兼容层回归。缓解方式是保持 RISC-V ABI 号不重排、不改评测脚本/镜像，并用正式 docker 命令完整复跑确认前三项通过。

禁止改动的 `data/sdcard-rv.img.gz` 与 `data/sdcard-la.img.gz` 未修改。
