# =============================================================================
# Helios64 AArch64 Microkernel Build System
# =============================================================================

LLVM_PREFIX ?= /opt/homebrew/opt/llvm/bin
LLD_PREFIX  ?= /opt/homebrew/opt/lld/bin

CC          := $(shell if [ -x "$(LLVM_PREFIX)/clang" ]; then echo "$(LLVM_PREFIX)/clang"; else echo "clang"; fi)
LD          := $(shell if [ -x "$(LLD_PREFIX)/ld.lld" ]; then echo "$(LLD_PREFIX)/ld.lld"; elif [ -x "$(LLVM_PREFIX)/ld.lld" ]; then echo "$(LLVM_PREFIX)/ld.lld"; else echo "ld.lld"; fi)
OBJCOPY     := $(shell if [ -x "$(LLVM_PREFIX)/llvm-objcopy" ]; then echo "$(LLVM_PREFIX)/llvm-objcopy"; else echo "llvm-objcopy"; fi)
OBJDUMP     := $(shell if [ -x "$(LLVM_PREFIX)/llvm-objdump" ]; then echo "$(LLVM_PREFIX)/llvm-objdump"; else echo "llvm-objdump"; fi)
SIZE        := $(shell if [ -x "$(LLVM_PREFIX)/llvm-size" ]; then echo "$(LLVM_PREFIX)/llvm-size"; else echo "size"; fi)
QEMU        := qemu-system-aarch64

TARGET      := aarch64-unknown-none-elf
BUILD_DIR   := build

CFLAGS      := --target=$(TARGET) \
               -O2 -g \
               -ffreestanding \
               -fno-builtin \
               -fno-stack-protector \
               -fno-pic -fno-pie \
               -mcpu=cortex-a72 \
               -Wall -Wextra -Werror \
               -Iinclude

ASFLAGS     := --target=$(TARGET) -g -mcpu=cortex-a72 -Iinclude
LDFLAGS     := -nostdlib -T linker.ld -z max-page-size=4096

ASM_SRCS    := kernel/boot.S \
               kernel/vectors.S

C_SRCS      := kernel/main.c \
               kernel/uart.c \
               kernel/kprintf.c \
               kernel/pmm.c \
               kernel/mmu.c \
               kernel/gic.c \
               kernel/timer.c \
               kernel/cap.c \
               kernel/ipc.c \
               kernel/sched.c \
               kernel/trap.c \
               user/ulib.c \
               user/uart_srv.c \
               user/sysmon_srv.c \
               user/shell.c

OBJS        := $(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SRCS)) \
               $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS))

ELF         := $(BUILD_DIR)/helios64.elf
BIN         := $(BUILD_DIR)/helios64.bin
DUMP        := $(BUILD_DIR)/helios64.asm

QEMU_FLAGS  := -M virt -cpu cortex-a72 -m 128M -nographic -kernel $(ELF)

.PHONY: all run test debug dump clean

all: $(ELF) $(BIN)

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@
	@$(SIZE) $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

dump: $(ELF)
	$(OBJDUMP) -d -S $< > $(DUMP)
	@echo "Disassembly written to $(DUMP)"

run: $(ELF)
	$(QEMU) $(QEMU_FLAGS)

test: $(ELF)
	@(sleep 0.5; printf "caps\nmem\nping\nuptime\nhalt\n") | $(QEMU) $(QEMU_FLAGS)

debug: $(ELF)
	$(QEMU) $(QEMU_FLAGS) -s -S

clean:
	rm -rf $(BUILD_DIR)
