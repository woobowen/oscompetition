# SeaOS 上手指南（Windows + WSL + Docker）

这份文档面向第一次 clone 本仓库的同伴，目标是让你能够：

1. 在本地完成 SeaOS 的基础构建与运行
2. 直接跑通仓库内已经准备好的公开 `oscomp` 自动测评环境
3. 知道当前仓库与初赛要求的主要差距

---

## 1. clone 后先看什么

建议先看这几个文件：

- [README.md](README.md)：当前仓库与初赛测评要求的差距分析
- [Makefile](Makefile)：构建与运行入口
- [common.mk](common.mk)：工具链配置

---

## 2. 需要提前安装的软件

推荐环境：

- Windows 11
- PowerShell
- WSL2（Ubuntu）
- Docker Desktop

当前仓库本地构建依赖 RISC-V 工具链与 QEMU：

- `riscv64-linux-gnu-gcc`
- `riscv64-linux-gnu-ld`
- `riscv64-linux-gnu-objcopy`
- `riscv64-linux-gnu-objdump`
- `qemu-system-riscv64`
- `make`

见 [common.mk:2-6](common.mk#L2-L6)。

如果你要先验证本地构建，请在 WSL 中确认这些命令存在：

```bash
riscv64-linux-gnu-gcc --version
qemu-system-riscv64 --version
make --version
```

---

## 3. 本地构建与运行 SeaOS

进入 WSL 后切到仓库目录：

```bash
cd /mnt/e/code/2_3_os/oskernel2026-seaos
```

构建：

```bash
make build
```

运行：

```bash
make run
```

当前默认构建产物：

- `target/kernel/kernel-qemu.elf`
- `target/mkfs/disk.img`

相关定义见：

- [Makefile:21-23](Makefile#L21-L23)
- [Makefile:128-130](Makefile#L128-L130)

---

## 4. 直接跑公开自动测评环境

当前仓库已经自带了跑公开测评所需的一部分内容：

- `autotest-for-oskernel/`

但下面这三个文件**没有上传到 GitLab**，同伴 clone 后需要自己准备：

- `autotest-for-oskernel/kernel.zip`
- `data/sdcard-rv.img.gz`
- `data/sdcard-la.img.gz`

### 4.1 需要自己补齐的文件

同伴需要确认并准备好下面这些路径：

- [autotest-for-oskernel/kernel.zip](autotest-for-oskernel/kernel.zip)
- [data/sdcard-rv.img.gz](data/sdcard-rv.img.gz)
- [data/sdcard-la.img.gz](data/sdcard-la.img.gz)

只有这三个文件补齐后，才能直接运行评测。

#### 4.1.1 准备缺失的评测资源

同伴 clone 仓库后，通常需要自己补这三个文件：

- `data/sdcard-rv.img.gz`
- `data/sdcard-la.img.gz`
- `autotest-for-oskernel/kernel.zip`

#### 4.1.2 准备 `sdcard-rv.img.gz` 与 `sdcard-la.img.gz`

先进入 `data/` 目录：

```bash
cd /mnt/e/code/2_3_os/oskernel2026-seaos/data
```

如果目录里还没有 `.xz` 文件，需要先下载：

```bash
wget https://github.com/oscomp/testsuits-for-oskernel/releases/download/pre-20250615/sdcard-rv.img.xz
wget https://github.com/oscomp/testsuits-for-oskernel/releases/download/pre-20250615/sdcard-la.img.xz
```

如果 `wget` 不方便，也可以用浏览器手动下载这两个文件，然后放到 `data/` 目录。

下载完成后，在 WSL 中执行：

```bash
unxz sdcard-la.img.xz
gzip sdcard-la.img
unxz sdcard-rv.img.xz
gzip sdcard-rv.img
```

执行完成后，`data/` 目录里应当能看到：

- `sdcard-rv.img.gz`
- `sdcard-la.img.gz`

#### 4.1.3 准备 `autotest-for-oskernel/kernel.zip`

进入评测仓库的 `kernel/` 目录：

```bash
cd /mnt/e/code/2_3_os/oskernel2026-seaos/autotest-for-oskernel/kernel
```

如果系统里有 `zip`，可以直接执行：

```bash
zip ../kernel.zip -r *
```

如果没有 `zip` 命令，推荐直接用 Python 打包：

```bash
python3 - <<'PY'
import os, zipfile
src = "."
dst = "../kernel.zip"
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(src):
        for name in files:
            path = os.path.join(root, name)
            arcname = os.path.relpath(path, src)
            z.write(path, arcname)
print("created", dst)
PY
```

执行完成后，应当能看到：

- [autotest-for-oskernel/kernel.zip](autotest-for-oskernel/kernel.zip)


### 4.2 拉取评测镜像

在 PowerShell 中执行：

```powershell
docker pull zhouzhouyi/os-contest:20260510
```

如果提示 Docker daemon 没启动，请先打开 Docker Desktop。

### 4.3 直接运行评测

在 PowerShell 中执行：

```powershell
docker run --rm `
  -v "E:\code\2_3_os\oskernel2026-seaos:/coursegrader/submit" `
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/coursegrader/testdata" `
  -v "E:\code\2_3_os\oskernel2026-seaos\autotest-for-oskernel:/cg" `
  -v "E:\code\2_3_os\oskernel2026-seaos\data:/mnt/cghook/" `
  zhouzhouyi/os-contest:20260510 python3 /cg/kernel.zip
```

如果你的仓库不在 `E:\code\2_3_os\oskernel2026-seaos`，请把上面命令里的路径改成你自己的实际 clone 路径。

---

## 5. 当前已知结果

当前仓库在公开评测环境下已经实测到的第一处失败是：

```text
make: *** No rule to make target 'all'. Stop.
```

也就是说：

- 公开评测环境已经能启动
- 当前 SeaOS 仓库已经被评测器真正执行到编译阶段
- 第一处明确不兼容点是 `Makefile` 没有 `all`

---

## 6. 当前仓库与初赛要求的简版差距

1. 没有 `make all`
2. 没有生成根目录 `kernel-rv`
3. 没有生成 `kernel-la`
4. 当前启动方式依赖 `-bios none`
5. 当前设计不是“扫描评测机挂载的 EXT4 测试盘并执行脚本”
6. syscall ABI 与比赛公开测试 ELF 大概率不兼容

详细分析请看 [README.md](README.md)。

---

## 7. 常见问题

### 7.1 Docker 报 `dockerDesktopLinuxEngine` 找不到

说明 Docker Desktop 没启动，或 Linux 容器引擎没起来。

先打开 Docker Desktop，再执行：

```powershell
docker version
```

确认同时能看到 Client 和 Server。

### 7.2 WSL 里 `apt install` 联网失败

这通常是 WSL 网络、代理或软件源问题，不一定是仓库问题。

如果你只是为了重新生成 `kernel.zip`，优先用文档中的 Python `zipfile` 方式，不必强依赖 `zip` 命令。
