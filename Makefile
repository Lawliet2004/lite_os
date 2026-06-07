PROJECT := LiteNix
BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso-root
KERNEL := $(BUILD_DIR)/litenix.elf
ISO := $(BUILD_DIR)/litenix.iso
SERIAL_LOG := $(BUILD_DIR)/serial.log

ifeq ($(origin CC),default)
CC := clang --target=x86_64-elf
endif
ifeq ($(origin AS),default)
AS := nasm
endif
ifeq ($(origin LD),default)
LD := ld.lld
endif
ifeq ($(origin OBJCOPY),default)
OBJCOPY := llvm-objcopy
endif

CFLAGS := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie
CFLAGS += -m64 -mcmodel=kernel -mno-red-zone -fno-omit-frame-pointer
CFLAGS += -mno-mmx -mno-sse -mno-sse2 -msoft-float
CFLAGS += -Wall -Wextra -Werror -O2 -g
CFLAGS += -Ikernel/include
TEST ?= none
ifeq ($(TEST),divide)
CFLAGS += -DLITENIX_TEST_DIVIDE_ERROR
endif
ifeq ($(TEST),pagefault)
CFLAGS += -DLITENIX_TEST_PAGE_FAULT
endif
LDFLAGS := -T kernel/arch/x86_64/linker.ld -nostdlib -static -z max-page-size=0x1000
ASFLAGS := -f elf64

C_SOURCES := \
	kernel/arch/x86_64/cpu/gdt.c \
	kernel/arch/x86_64/interrupt/idt.c \
	kernel/arch/x86_64/interrupt/interrupt.c \
	kernel/arch/x86_64/interrupt/pic.c \
	kernel/drivers/pit.c \
	kernel/mm/pmm.c \
	kernel/core/init.c \
	kernel/core/kernel.c \
	kernel/core/panic.c \
	kernel/core/printk.c \
	kernel/drivers/serial.c \
	kernel/drivers/vga_text.c \
	kernel/lib/string.c

ASM_SOURCES := \
	kernel/arch/x86_64/entry.S \
	kernel/arch/x86_64/cpu/gdt_load.S \
	kernel/arch/x86_64/interrupt/isr.S

OBJECTS := \
	$(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES)) \
	$(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

.PHONY: all iso run debug-gdb clean distclean

all: $(KERNEL)

$(KERNEL): $(OBJECTS) kernel/arch/x86_64/linker.ld
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

iso: $(ISO)

$(ISO): $(KERNEL) boot/limine.conf
	@mkdir -p $(ISO_ROOT)/boot
	cp $(KERNEL) $(ISO_ROOT)/boot/litenix.elf
	cp boot/limine.conf $(ISO_ROOT)/boot/limine.conf
	cp toolchain/limine/limine-bios.sys $(ISO_ROOT)/boot/
	cp toolchain/limine/limine-bios-cd.bin $(ISO_ROOT)/boot/
	xorriso -as mkisofs -b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		$(ISO_ROOT) -o $(ISO)
	toolchain/limine/limine bios-install $(ISO)

run: $(ISO)
	@mkdir -p $(BUILD_DIR)
	qemu-system-x86_64 -M q35 -m 64M -cdrom $(ISO) \
		-serial file:$(SERIAL_LOG) -debugcon stdio -no-reboot -no-shutdown

debug-gdb: $(ISO)
	qemu-system-x86_64 -M q35 -m 64M -cdrom $(ISO) \
		-serial file:$(SERIAL_LOG) -S -s -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
