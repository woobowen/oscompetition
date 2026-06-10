# 引入通用配置文件
include common.mk

# 配置CPU核心数量
CPUNUM = 1

# 定义目标文件输出目录
TARGET = target

# 定义各模块路径
KernelPath = src/kernel
UserPath   = src/user
MKFSPath   = src/mkfs
LoaderPath = src/loader

# 链接脚本
KERNEL_LD = $(LoaderPath)/kernel.ld
USER_LD   = $(LoaderPath)/user.ld

# 定义目标文件路径
ELFKernel  = $(TARGET)/kernel/kernel-qemu.elf
ELFKernelLA = $(TARGET)/loongarch/kernel-la.elf
ELFUser    = $(UserPath)/initcode.h
DISKIMG    = $(TARGET)/mkfs/disk.img

# 主机工具编译器
HOSTCC ?= gcc
HOSTCFLAGS ?= -O2 -Wall -Werror
BIN2C_SRC = tools/bin2c.c
BIN2C = $(TARGET)/tools/bin2c
LA_ELFGEN_SRC = tools/la_elfgen.c
LA_EARLY_BOOT_HDR = $(KernelPath)/loongarch/early_boot.h
LA_SOURCE_HDR = $(LA_EARLY_BOOT_HDR) $(KernelPath)/loongarch/proc.h $(KernelPath)/loongarch/trap.h $(KernelPath)/loongarch/trap_layout.h
LA_ELFGEN = $(TARGET)/tools/la_elfgen
LA_TOOLPREFIX ?= loongarch64-linux-gnu-
LA_CC = $(LA_TOOLPREFIX)gcc
LA_LD = $(LA_TOOLPREFIX)ld
LA_BUILD_MODE ?= $(if $(shell command -v $(LA_CC) >/dev/null 2>&1 && command -v $(LA_LD) >/dev/null 2>&1 && echo yes),source,stub)
LA_KERNEL_LD = $(KernelPath)/loongarch/kernel.ld
LA_SOURCE_FILE = $(wildcard $(KernelPath)/loongarch/*.S) $(filter-out $(KernelPath)/loongarch/userret.c,$(wildcard $(KernelPath)/loongarch/*.c))
LA_SOURCE_OBJ = $(patsubst $(KernelPath)/loongarch/%.S,$(TARGET)/loongarch/%.o,$(filter %.S,$(LA_SOURCE_FILE)))
LA_SOURCE_OBJ += $(patsubst $(KernelPath)/loongarch/%.c,$(TARGET)/loongarch/%.o,$(filter %.c,$(LA_SOURCE_FILE)))
LA_SOURCE_OBJ += $(TARGET)/loongarch/userret_c.o
LA_CFLAGS = -Wall -Werror -O2 -ffreestanding -fno-common -nostdlib -fno-stack-protector -fno-pie -I.
LA_LDFLAGS = -z max-page-size=4096

# 收集内核源文件 (.c .S，包括子目录)
KernelSourceFile = $(wildcard $(KernelPath)/*.c) $(wildcard $(KernelPath)/*.S)
KernelSourceFile += $(wildcard $(KernelPath)/*/*.c) $(wildcard $(KernelPath)/*/*.S)
KernelSourceFile := $(filter-out $(KernelPath)/loongarch/%, $(KernelSourceFile))

# 内核目标文件
KernelOBJ = $(patsubst $(KernelPath)/%.S, $(TARGET)/kernel/%.o, $(filter %.S, $(KernelSourceFile)))
KernelOBJ += $(patsubst $(KernelPath)/%.c, $(TARGET)/kernel/%.o, $(filter %.c, $(KernelSourceFile)))

# 用户程序分类: 启动程序(initcode.c) 通用库(syscall.c、help.c) 测试程序(test_1.c test_2.c ...)
USER_INIT_C = $(UserPath)/initcode.c
USER_LIB_C = $(UserPath)/syscall.c $(UserPath)/help.c
USER_TEST_C = $(filter-out $(USER_INIT_C) $(USER_LIB_C) $(LA_INITCODE_C), $(wildcard $(UserPath)/*.c))

# 用户目标文件
USER_INIT_OBJ = $(TARGET)/user/initcode.o
USER_LIB_OBJ  = $(USER_LIB_C:$(UserPath)/%.c=$(TARGET)/user/%.o)
USER_TEST_OBJ = $(USER_TEST_C:$(UserPath)/%.c=$(TARGET)/user/%.o)
USER_TEST_ELF = $(USER_TEST_C:$(UserPath)/%.c=$(TARGET)/user/%.elf)

.SECONDARY: $(USER_LIB_OBJ) $(USER_TEST_OBJ)

# QEMU 模拟器配置
QEMU     = qemu-system-riscv64
QEMU_LA  = qemu-system-loongarch64
QEMUOPTS = -machine virt -bios default -kernel $(ELFKernel)
QEMUOPTS += -m 128M -smp $(CPUNUM) -nographic -serial mon:stdio -d guest_errors,cpu_reset -D qemu.log
QEMUOPTS += -drive file=$(DISKIMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
LA_SERIAL_LOG = os_serial_out_la.txt
LA_QEMU_TIMEOUT = 8s
LA_USER_LD = $(LoaderPath)/user-la.ld
LA_INITCODE_C = $(UserPath)/initcode_la.c
LA_INITCODE_H = $(KernelPath)/loongarch/initcode_la.h
LA_OBJCOPY = $(LA_TOOLPREFIX)objcopy
LA_USER_CFLAGS = -Wall -O2 -ffreestanding -fno-common -nostdlib -fno-stack-protector -fno-pie -fno-builtin -I.

# 调试配置
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

# 生成 .gdbinit
.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

# 创建输出目录
.PHONY: $(TARGET)
$(TARGET):
	@mkdir -p $(TARGET)/kernel
	@mkdir -p $(TARGET)/kernel/arch
	@mkdir -p $(TARGET)/kernel/boot
	@mkdir -p $(TARGET)/kernel/lock
	@mkdir -p $(TARGET)/kernel/lib
	@mkdir -p $(TARGET)/kernel/mem
	@mkdir -p $(TARGET)/kernel/trap
	@mkdir -p $(TARGET)/kernel/proc
	@mkdir -p $(TARGET)/kernel/syscall
	@mkdir -p $(TARGET)/kernel/fs
	@mkdir -p $(TARGET)/loongarch
	@mkdir -p $(TARGET)/user
	@mkdir -p $(TARGET)/mkfs
	@mkdir -p $(TARGET)/tools

# 内核编译规则
$(TARGET)/kernel/%.o: $(KernelPath)/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET)/kernel/%.o: $(KernelPath)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 用户程序编译规则

# initcode.o（特殊）
$(USER_INIT_OBJ): $(USER_INIT_C)
	$(CC) $(CFLAGS) -I. -c -o $@ $<

# 通用库和测试程序的 .o（共享规则）
$(TARGET)/user/%.o: $(UserPath)/%.c
	$(CC) $(CFLAGS) -I. -c -o $@ $<

# 链接普通用户程序
$(TARGET)/user/%.elf: $(TARGET)/user/%.o $(USER_LIB_OBJ)
	$(LD) $(LDFLAGS) -T $(USER_LD) $< $(USER_LIB_OBJ) -o $@

# 生成 initcode.h（供内核嵌入）
$(ELFUser): $(USER_INIT_OBJ) $(BIN2C)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $(TARGET)/user/initcode.out $<
	$(OBJCOPY) -S -O binary $(TARGET)/user/initcode.out $(TARGET)/user/initcode
	$(BIN2C) $(TARGET)/user/initcode $(ELFUser)

# 构建主机端的二进制转头文件工具
$(BIN2C): $(BIN2C_SRC) | $(TARGET)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

# 构建主机端的 LoongArch 最小 ELF 生成工具
$(LA_ELFGEN): $(LA_ELFGEN_SRC) $(LA_EARLY_BOOT_HDR) | $(TARGET)
	$(HOSTCC) $(HOSTCFLAGS) -I. -o $@ $<

# LoongArch initcode: compile → link at 0 → binary → C header
$(LA_INITCODE_H): $(LA_INITCODE_C) $(LA_USER_LD) $(BIN2C) | $(TARGET)
	$(LA_CC) $(LA_USER_CFLAGS) -c -o $(TARGET)/loongarch/initcode_la.o $<
	$(LA_LD) $(LA_LDFLAGS) -N -e main -T $(LA_USER_LD) -o $(TARGET)/loongarch/initcode_la.out $(TARGET)/loongarch/initcode_la.o
	$(LA_OBJCOPY) -S -O binary $(TARGET)/loongarch/initcode_la.out $(TARGET)/loongarch/initcode_la
	$(BIN2C) $(TARGET)/loongarch/initcode_la $@

# LoongArch 源码构建路径（有 loongarch64-linux-gnu-* 工具链时启用）
$(TARGET)/loongarch/%.o: $(KernelPath)/loongarch/%.S $(LA_SOURCE_HDR) | $(TARGET)
	$(LA_CC) $(LA_CFLAGS) -c -o $@ $<

$(TARGET)/loongarch/%.o: $(KernelPath)/loongarch/%.c $(LA_SOURCE_HDR) | $(TARGET)
	$(LA_CC) $(LA_CFLAGS) -c -o $@ $<

$(TARGET)/loongarch/userret_c.o: $(KernelPath)/loongarch/userret.c $(LA_SOURCE_HDR) | $(TARGET)
	$(LA_CC) $(LA_CFLAGS) -c -o $@ $<

# 生成 kernel-qemu.elf
$(ELFKernel): $(KernelOBJ) $(ELFUser)
	$(LD) $(LDFLAGS) -T $(KERNEL_LD) $(KernelOBJ) -o $@

ifeq ($(LA_BUILD_MODE),source)
# 生成 LoongArch 源码启动 ELF
$(ELFKernelLA): $(LA_SOURCE_OBJ) $(LA_KERNEL_LD) $(LA_INITCODE_H) | $(TARGET)
	$(LA_LD) $(LA_LDFLAGS) -T $(LA_KERNEL_LD) $(LA_SOURCE_OBJ) -o $@
else ifeq ($(LA_BUILD_MODE),stub)
# 生成 LoongArch 最小启动 ELF
$(ELFKernelLA): $(LA_ELFGEN) | $(TARGET)
	$(LA_ELFGEN) $@
else
$(error unsupported LA_BUILD_MODE=$(LA_BUILD_MODE), use source or stub)
endif

# 生成磁盘映像（包含所有普通用户程序）
$(DISKIMG): $(USER_TEST_ELF)
	gcc -Werror -Wall -I. -o $(TARGET)/mkfs/mkfs $(MKFSPath)/mkfs.c
	$(TARGET)/mkfs/mkfs $@ $(USER_TEST_ELF)

# 新增 all 目标以兼容评测系统调用 `make all`
.PHONY: all
all: build build-la
	@cp $(ELFKernel) kernel-rv
	@cp $(ELFKernelLA) kernel-la
	@echo "===== make all: kernel-rv generated ====="
	@echo "===== make all: kernel-la generated ====="

# 构建目标
.PHONY: build
build: $(TARGET) $(ELFUser) $(USER_TEST_ELF) $(ELFKernel) $(DISKIMG)
	@echo "===== make success! ====="

# LoongArch B 线：当前生成可被 QEMU 加载并进入早期架构脚手架的最小 ELF
.PHONY: build-la
build-la: $(TARGET) $(ELFKernelLA)
	@echo "===== make build-la success ($(LA_BUILD_MODE))! ====="

# 运行目标
.PHONY: run
run: build
	$(QEMU) $(QEMUOPTS)

.PHONY: run-la
run-la: build-la $(DISKIMG)
	$(QEMU_LA) -kernel $(ELFKernelLA) -m 1G -nographic -smp $(CPUNUM) -drive file=$(DISKIMG),if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 -no-reboot -device virtio-net-pci,netdev=net0 -netdev user,id=net0 -rtc base=utc

.PHONY: check-la
check-la: all
	timeout $(LA_QEMU_TIMEOUT) $(QEMU_LA) -kernel kernel-la -m 1G -display none -serial file:$(LA_SERIAL_LOG) -smp $(CPUNUM) -drive file=$(DISKIMG),if=none,format=raw,id=x0 -device virtio-blk-pci,drive=x0 || test $$? -eq 124
	@cat $(LA_SERIAL_LOG)
	@grep -q "loongarch boot start" $(LA_SERIAL_LOG)
	@grep -q "\[init\] kernel ready" $(LA_SERIAL_LOG)
	@echo "===== make check-la success! ====="

# 调试目标
.PHONY: debug
debug: build .gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

# 清理目标
.PHONY: clean
clean:
	rm -rf $(TARGET)
	rm -f $(UserPath)/initcode.h
	rm -f .gdbinit
