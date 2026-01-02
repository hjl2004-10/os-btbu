# 附录 B：常见工具的使用方法 (Rust版本)

## 分析可执行文件

对于Rust编译器生成的执行程序，可通过各种有效工具进行分析。掌握这些工具的使用方法，有助于在后续开发中灵活处理和解决各种问题。

我们以一个简单的Rust “Hello, world” 应用的可执行文件为例进行分析。

### file 工具

首先使用 `file` 工具查看生成的可执行文件格式：

```bash
$ cargo new os
$ cd os; cargo build
$ file target/debug/os
```
输出示例：
```
target/debug/os: ELF 64-bit LSB shared object, x86-64, version 1 (SYSV), dynamically linked,
interpreter /lib64/ld-linux-x86-64.so.2, ......
```

可以看到可执行文件的格式为 **ELF (Executable and Linkable Format)**，硬件平台是 x86-64。ELF文件包含代码段、数据段以及描述这些段在地址空间和文件中位置、权限控制信息的元数据。

### rust-readobj

使用 `rust-readobj` 工具查看ELF文件的具体内容：

```bash
$ rust-readobj -all target/debug/os
```

主要输出内容：

1. **ELF header**（位于文件开头）：
   - 魔数(Magic)：`(7F 45 4C 46)`，用于快速确认文件是否为ELF格式
   - 入口点(Entry)：`0x5070`
   - 包含12个program header和42个section header的信息

2. **Section header示例**（如代码段.text）：
   - 需要被加载到地址 `0x5070`
   - 大小为208067字节
   - 由元数据的Offset、Size和Address字段给出具体信息

3. **符号表**：包含 `_start` 函数（地址 `0x5070`）和 `main` 函数（地址 `0x51a0`）等信息

ELF文件内容按顺序为：ELF header → 若干个program header → 程序各个段的实际数据 → 若干个section header。

### rust-objdump

使用 `rust-objdump` 工具反汇编ELF文件，查看具体指令内容：

```bash
$ rust-objdump -all target/debug/os
```

输出片段：
```
0000000000005070 <_start>:
   5070: f3 0f 1e fa                   endbr64
   5074: 31 ed                         xorl    %ebp, %ebp
   ...

00000000000051a0 <main>:
   51a0: 48 83 ec 18                   subq    $24, %rsp
   51a4: 8a 05 db 7a 03 00             movb    228059(%rip), %al
   ...
```

可以查看用户态执行环境入口函数 `_start` 和应用程序主函数 `main` 的具体汇编代码。

### rust-objcopy

使用 `rust-objcopy` 工具清除ELF文件中与执行无直接关系的信息（如调试信息）：

```bash
$ rust-objcopy --strip-all target/debug/os target/debug/os.bin
$ ls -l target/debug/os*
$ ./target/debug/os.bin  # 输出: Hello, world!
```

处理后的文件大小显著减小，但仍能正常执行。

生成纯二进制镜像文件：
```bash
$ rust-objcopy --strip-all target/debug/os -O binary target/debug/os.bin
```

此操作会删除所有header，只保留各段的实际数据，得到一个没有任何符号的纯二进制镜像文件。

## qemu平台上可执行文件和二进制镜像的生成流程

### Makefile配置

```makefile
TARGET := riscv64gc-unknown-none-elf
MODE := release
KERNEL_ELF := target/$(TARGET)/$(MODE)/os
KERNEL_BIN := $(KERNEL_ELF).bin

$(KERNEL_BIN): kernel
   @$(OBJCOPY) $(KERNEL_ELF) --strip-all -O binary $@

kernel:
   @cargo build --release
```

- `KERNEL_ELF`：可执行文件os的路径
- `KERNEL_BIN`：只保留段数据的二进制镜像文件os.bin的路径
- 目标`kernel`：通过`cargo build --release`生成可执行文件
- 目标`KERNEL_BIN`：依赖`kernel`目标，通过`rust-objcopy`移除所有header和符号得到二进制镜像

### 运行qemu

```makefile
KERNEL_ENTRY_PA := 0x80020000
BOARD ?= qemu
SBI ?= rustsbi
BOOTLOADER := ../bootloader/$(SBI)-$(BOARD).bin

run-inner: build
ifeq ($(BOARD),qemu)
   @qemu-system-riscv64 \
      -machine virt \
      -nographic \
      -bios $(BOOTLOADER) \
      -device loader,file=$(KERNEL_BIN),addr=$(KERNEL_ENTRY_PA)
else
   # k210平台的处理
endif
```

**qemu参数说明：**
- `-machine virt`：使用预设的硬件配置
- `-bios`：指定bootloader
- `-device loader,file=...,addr=...`：将二进制镜像加载到内存指定位置

**退出qemu**：先按`Ctrl+A`，再按`X`

## k210平台上可执行文件和二进制镜像的生成流程

对于k210平台，将开发板连接到PC后执行：
```bash
make run BOARD=k210
```

Makefile中k210平台的相关配置：
```makefile
K210-SERIALPORT = /dev/ttyUSB0
K210-BURNER = ../tools/kflash.py

# 在run-inner目标中
else  # BOARD=k210的情况
   @cp $(BOOTLOADER) $(BOOTLOADER).copy
   @dd if=$(KERNEL_BIN) of=$(BOOTLOADER).copy bs=128K seek=1
   @mv $(BOOTLOADER).copy $(KERNEL_BIN)
   @sudo chmod 777 $(K210-SERIALPORT)
   python3 $(K210-BURNER) -p $(K210-SERIALPORT) -b 1500000 $(KERNEL_BIN)
   miniterm --eol LF --dtr 0 --rts 0 --filter direct $(K210-SERIALPORT) 115200
endif
```

关键步骤：
- 使用`dd`工具将bootloader和二进制镜像拼接
- 通过`kflash.py`工具将镜像烧录到k210开发板
- 使用`miniterm`建立串口连接