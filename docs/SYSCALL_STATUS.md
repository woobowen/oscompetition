# SeaOS syscall 状态台账（A 线：RISC-V Linux 兼容）

> 唯一权威的 syscall 进度表。每补/改一个就更新对应行。号 = Linux/RISC-V ABI。
> 返回值约定：成功返回结果（uint64）；失败返回 (uint64)(-EXXX)，见 docs/DECISIONS.md D1/D2。
> 分发表上限 SYS_MAX_NUM=502（src/kernel/syscall/type.h）。

## 已实现
| 号 | 名 | 备注 |
|---|---|---|
| 4 | fork | SeaOS |
| 17 | print_cwd | SeaOS 私有 |
| 23 | dup | |
| 34 | mkdir(at) | |
| 35 | unlink(at) | |
| 37 | link(at) | |
| 49 | chdir | |
| 56 | open(at) | |
| 57 | close | |
| 61 | getdents64 | |
| 62 | lseek | |
| 63 | read | |
| 64 | write | |
| 80 | fstat | |
| 93 | exit | |
| 96 | set_tid_address | 返回 pid（D3 最小实现） |
| 101 | nanosleep(兼容) | |
| 172 | getpid | |
| 214 | brk | |
| 215 | munmap | |
| 221 | execve | |
| 222 | mmap | |
| 260 | wait4 | |
| 500/501/502 | schedstat/spawn/shutdown | SeaOS 私有 |

## 进行中 / 下一个
| 号 | 名 | 状态 | 计划 |
|---|---|---|---|
| （待 docker 评测暴露） | | | |

## 已知缺口链（待 docker 复跑后填充）
> Spec #1 落地"未知 syscall 返回 -ENOSYS"后，跑 unixbench_testcode.sh，
> 把日志里新出现的 `unknown syscall N` 逐个登记到这里，再按批补齐。

（注：Docker 评测环境缓存问题导致无法获取新缺口链，需在真实评测环境中验证）
