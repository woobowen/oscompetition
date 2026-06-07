# SeaOS RISC-V 当前状态（2026-06-07）

## 总体进度

**正式 docker 评测命令已跑通前四个 RISC-V 测试组：`unixbench-musl`、`busybox-musl`、`cyclictest-musl`、`netperf-musl`。** 第五项 `lmbench-musl` 已进入运行阶段，但不属于本轮通过标准。

本轮验证命令：

```bash
docker run --rm \
  -v "E:\code\2_3_os\oskernel2026-seaos:/coursegrader/submit" \
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/coursegrader/testdata" \
  -v "E:\code\2_3_os\oskernel2026-seaos\autotest-for-oskernel:/cg" \
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/mnt/cghook/" \
  zhouzhouyi/os-contest:20260510 python3 /cg/kernel.zip
```

评测时间：2026-06-07 17:27:33 至 18:24:42（Asia/Shanghai）。编译段显示 `make all` 成功，`kernel-rv` 与 `kernel-la` 均生成；RISC-V 串口日志已写入根目录 `os_serial_out_rv.txt`。

## 最新测试结论

| 测试组 | 当前结论 | 证据 |
|---|---|---|
| unixbench-musl | 通过 | `#### OS COMP TEST GROUP END unixbench-musl ####` 后出现 `======== test sucess ========` |
| busybox-musl | 通过 | 组尾出现 `======== test sucess ========` |
| cyclictest-musl | 通过 | 四个子项均 `end: success`，组尾出现 `======== test sucess ========` |
| netperf-musl | 通过 | 五个 netperf 子项均 `end: success`，组尾出现 `======== test sucess ========` |
| lmbench-musl | 后续目标 | 第五组已启动到 `latency measurements`，不作为本轮验收标准 |

netperf 关键输出：

```text
====== netperf UDP_STREAM end: success ======
====== netperf TCP_STREAM end: success ======
====== netperf UDP_RR end: success ======
====== netperf TCP_RR end: success ======
====== netperf TCP_CRR end: success ======
#### OS COMP TEST GROUP END netperf-musl ####

======== test sucess ========
```

本轮 `os_serial_out_rv.txt` 在第四组结束前未出现 `unknown syscall`、`panic`、`[SEGV]`。

## 本轮关键修复

| 修复 | 主要文件 | 效果 |
|---|---|---|
| 新增最小 AF_INET loopback socket 后端 | `src/kernel/fs/socket.c` | 支持 netperf 使用 `127.0.0.1:12865` 完成 TCP/UDP stream 与 RR/CRR |
| `file_t` 接入 socket 生命周期 | `src/kernel/fs/type.h`, `src/kernel/fs/method.h`, `src/kernel/fs/fs.c` | socket fd 支持 close/read/write/dup/fstat，fork 后引用共享，close/shutdown 唤醒阻塞端 |
| 补齐 socket syscall 族 | `src/kernel/syscall/type.h`, `src/kernel/syscall/method.h`, `src/kernel/syscall/syscall.c`, `src/kernel/syscall/sysfunc.c` | 接入 `socket/bind/listen/accept/connect/sendto/recvfrom/getsockopt/.../accept4` |
| 实现 `pselect6(72)` | `src/kernel/syscall/sysfunc.c` | 支持 Linux fd_set copyin/copyout；socket 使用真实 readiness |
| 控制 socket 静态资源规模 | `src/kernel/fs/socket.c` | 避免大 BSS/缓冲池消耗物理页，保护已通过的 UnixBench/cyclictest |

`sendmsg(211)`、`recvmsg(212)` 当前明确返回 `-EOPNOTSUPP`；实测 netperf 未进入该路径。

## 语义边界

- 本轮只考虑 RISC-V A 线。
- socket 子系统只实现本机 loopback，不实现真实网卡、路由、IPv6、多播或 out-of-loopback 通信。
- 支持的 socket 范围限定为 `AF_INET`、`SOCK_STREAM`/`SOCK_DGRAM`、`IPPROTO_TCP`/`IPPROTO_UDP`/`0`。
- `SO_REUSEADDR`、`SO_SNDBUF`、`SO_RCVBUF`、`SO_KEEPALIVE`、`SO_DONTROUTE`、`TCP_NODELAY`、`TCP_MAXSEG`、`TCP_CORK` 采用最小兼容；`SO_SNDBUF/SO_RCVBUF` 返回至少 `32000`，`TCP_MAXSEG` 返回 `1460`。
- 第五组 `lmbench-musl` 后续若暴露新缺口，应单独记录和收敛。

## 公共文件风险说明

本轮改动涉及 AGENTS.md 标记的公共文件 `src/kernel/syscall/type.h` 与 `src/kernel/syscall/syscall.c`。风险是 Linux/RISC-V syscall 号、分发表入口或返回语义回归；缓解方式是只追加 ABI 号和分发表项，不重排既有入口，并用正式 docker 命令完整复跑确认前四项通过。

同时改动了 syscall/FS 公共路径 `src/kernel/syscall/sysfunc.c`、`src/kernel/syscall/method.h`、`src/kernel/fs/fs.c`、`src/kernel/fs/type.h`、`src/kernel/fs/method.h`、`src/kernel/lib/errno.h`。这些改动可能影响 fd 生命周期、stat 类型、read/write/close 行为和 errno 返回；当前通过正式评测验证了前三项无回退、第四项通过。

禁止改动的 `data/sdcard-rv.img.gz` 与 `data/sdcard-la.img.gz` 未修改。
