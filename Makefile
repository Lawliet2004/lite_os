PROJECT := LiteNix
BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso-root
KERNEL := $(BUILD_DIR)/litenix.elf
ISO := $(BUILD_DIR)/litenix.iso
SERIAL_LOG := $(BUILD_DIR)/serial.log
SERIAL_LOG_NEG := $(BUILD_DIR)/serial-neg.log
SERIAL_LOG_HEAP := $(BUILD_DIR)/serial-heap.log
SERIAL_LOG_VMM := $(BUILD_DIR)/serial-vmm.log
ifeq ($(OS),Windows_NT)
LIMINE ?= toolchain/limine/limine.exe
else
LIMINE ?= toolchain/limine/limine
endif

ifeq ($(origin CC),default)
ifeq ($(OS),Windows_NT)
CC := zig cc -target x86_64-freestanding-none
else
CC := clang --target=x86_64-elf
endif
endif
ifeq ($(origin AS),default)
AS := nasm
endif
ifeq ($(origin LD),default)
ifeq ($(OS),Windows_NT)
LD := zig cc -target x86_64-freestanding-none
else
LD := ld.lld
endif
endif
ifeq ($(origin OBJCOPY),default)
OBJCOPY := llvm-objcopy
endif

CFLAGS := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie
CFLAGS += -m64 -mcmodel=kernel -mno-red-zone -fno-omit-frame-pointer
CFLAGS += -mno-mmx -mno-sse -mno-sse2 -msoft-float
CFLAGS += -Wall -Wextra -Werror -O2 -g
CFLAGS += -Ikernel/include

USER_CFLAGS := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie
USER_CFLAGS += -m64 -fno-omit-frame-pointer -mno-mmx -mno-sse -mno-sse2
USER_CFLAGS += -Wall -Wextra -Werror -O2 -g
USER_CFLAGS += -Iuser/libc-lite

# Test modes. All default to off so plain `make` builds the normal kernel.
# `make verify-boot` only runs against TEST=none; the negative modes are run
# explicitly via `make verify-boot-neg` / `make verify-boot-all`.
TEST ?= none
ifeq ($(TEST),divide)
CFLAGS += -DLITENIX_TEST_DIVIDE_ERROR
endif
ifeq ($(TEST),pagefault)
CFLAGS += -DLITENIX_TEST_PAGE_FAULT
endif
ifeq ($(TEST),vmm-fault)
# Force a kernel-side user-pointer copy that should never be reached.
# The kernel still takes the null-pointer exception via the VMM self-test
# pre-arming: the smoke path dereferences an unmapped user address.
CFLAGS += -DLITENIX_TEST_VMM_FAULT
endif
ifeq ($(TEST),heap-panic)
# Disable the negative self-test and instead force a heap panic at boot by
# corrupting an alloc header on purpose. The panic path is verified by
# `verify-boot-heap` which checks for the expected KERNEL PANIC banner.
CFLAGS += -DLITENIX_TEST_HEAP_PANIC
endif

LDFLAGS := -T kernel/arch/x86_64/linker.ld -nostdlib -static -z max-page-size=0x1000
ASFLAGS := -f elf64

C_SOURCES := \
	kernel/arch/x86_64/cpu/gdt.c \
	kernel/arch/x86_64/interrupt/idt.c \
	kernel/arch/x86_64/interrupt/interrupt.c \
	kernel/arch/x86_64/interrupt/pic.c \
	kernel/arch/x86_64/memory/vmm.c \
	kernel/drivers/pit.c \
	kernel/mm/heap.c \
	kernel/mm/pmm.c \
	kernel/core/init.c \
	kernel/core/kernel.c \
	kernel/core/panic.c \
	kernel/core/printk.c \
	kernel/drivers/serial.c \
	kernel/drivers/vga_text.c \
	kernel/lib/string.c \
	kernel/sched/scheduler.c \
	kernel/sched/task.c \
	kernel/sched/wait_queue.c \
	kernel/arch/x86_64/syscall/syscall_entry.c \
	kernel/sys/syscall_table.c \
	kernel/sys/sys_exit.c \
	kernel/sys/sys_file.c \
	kernel/sys/sys_process.c \
	kernel/sys/sys_mem.c \
	kernel/mm/uaccess.c \
	kernel/core/elf_loader.c \
	kernel/fs/vfs.c \
	kernel/fs/initramfs.c \
	kernel/fs/ext2.c \
	kernel/drivers/pci.c \
	kernel/drivers/virtio_net.c \
	kernel/net/net.c \
	kernel/net/eth.c \
	kernel/net/arp.c \
	kernel/net/ipv4.c \
	kernel/net/icmp.c \
	kernel/net/udp.c \
	kernel/net/tcp.c \
	kernel/net/socket.c

ASM_SOURCES := \
	kernel/arch/x86_64/entry.S \
	kernel/arch/x86_64/cpu/gdt_load.S \
	kernel/arch/x86_64/interrupt/isr.S \
	kernel/arch/x86_64/memory/switch.S \
	kernel/arch/x86_64/syscall/syscall_stub.S \
	kernel/lib/setjmp.S \
	kernel/core/user_binaries.S \
	kernel/fs/initramfs_binary.S

OBJECTS := \
	$(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES)) \
	$(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

# Raw-syscall test ELFs (no libc, no CRT)
RAWSYSCALL_TESTS := \
	$(BUILD_DIR)/tests/raw/test_write_exit.elf \
	$(BUILD_DIR)/tests/raw/test_read.elf \
	$(BUILD_DIR)/tests/raw/test_stat.elf \
	$(BUILD_DIR)/tests/raw/test_mmap.elf \
	$(BUILD_DIR)/tests/raw/test_mmap_file.elf \
	$(BUILD_DIR)/tests/raw/test_process.elf \
	$(BUILD_DIR)/tests/raw/test_clock.elf \
	$(BUILD_DIR)/tests/raw/test_getrandom.elf \
	$(BUILD_DIR)/tests/raw/test_all.elf

# CFLAGS for no-libc raw-syscall tests:
# same arch flags but no kernel-only restrictions
RAW_CFLAGS := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie
RAW_CFLAGS += -m64 -fno-omit-frame-pointer -mno-mmx -mno-sse -mno-sse2
RAW_CFLAGS += -Wall -Wextra -Werror -O2 -g

.PHONY: all iso run verify-boot verify-boot-neg verify-boot-heap \
        verify-boot-vmm verify-boot-all debug-gdb clean distclean \
        userspace-tests

all: $(KERNEL)

$(KERNEL): $(OBJECTS) kernel/arch/x86_64/linker.ld
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/kernel/core/user_binaries.o: $(BUILD_DIR)/user/test_read_kernel.elf $(BUILD_DIR)/user/test_privileged.elf $(BUILD_DIR)/user/init.elf

$(BUILD_DIR)/kernel/fs/initramfs_binary.o: $(BUILD_DIR)/initramfs.tar

hello_musl: hello_musl.c
	C:\msys64\clang64\bin\zig.exe cc -target x86_64-linux-musl -static hello_musl.c -o hello_musl

# Dynamic musl binary - built with zig dynamically linking musl
$(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic: tests/userspace/dynamic-musl/hello_dynamic.c
	@mkdir -p $(dir $@)
	C:\msys64\clang64\bin\zig.exe cc -target x86_64-linux-musl -dynamic $< -o $@
	MSYS_NO_PATHCONV=1 python3 scripts/patch_interpreter.py $@ $(BUILD_DIR)/tests/userspace/dynamic-musl/missing_interp /lib/ld-musl-x86_64.so.1 /lib/nonexistent.so || MSYS_NO_PATHCONV=1 python scripts/patch_interpreter.py $@ $(BUILD_DIR)/tests/userspace/dynamic-musl/missing_interp /lib/ld-musl-x86_64.so.1 /lib/nonexistent.so
	MSYS_NO_PATHCONV=1 python3 scripts/patch_interpreter.py $@ $(BUILD_DIR)/tests/userspace/dynamic-musl/invalid_interp /lib/ld-musl-x86_64.so.1 /hello.txt || MSYS_NO_PATHCONV=1 python scripts/patch_interpreter.py $@ $(BUILD_DIR)/tests/userspace/dynamic-musl/invalid_interp /lib/ld-musl-x86_64.so.1 /hello.txt

$(BUILD_DIR)/initramfs.tar: $(BUILD_DIR)/user/init.elf $(BUILD_DIR)/user/sh.elf $(RAWSYSCALL_TESTS) hello_musl $(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic
	@mkdir -p $(BUILD_DIR)/initramfs-root/bin
	@mkdir -p $(BUILD_DIR)/initramfs-root/tests
	@mkdir -p $(BUILD_DIR)/initramfs-root/lib
	@mkdir -p $(BUILD_DIR)/initramfs-root/lib64
	cp $(BUILD_DIR)/user/init.elf $(BUILD_DIR)/initramfs-root/bin/init
	cp $(BUILD_DIR)/user/sh.elf $(BUILD_DIR)/initramfs-root/bin/sh
	@echo "Hello, LiteNix VFS!" > $(BUILD_DIR)/initramfs-root/hello.txt
	@echo "This is a test file in /bin" > $(BUILD_DIR)/initramfs-root/bin/test.txt
	cp hello_musl $(BUILD_DIR)/initramfs-root/bin/hello_musl
	cp $(BUILD_DIR)/tests/raw/test_write_exit.elf  $(BUILD_DIR)/initramfs-root/tests/test_write_exit
	cp $(BUILD_DIR)/tests/raw/test_read.elf        $(BUILD_DIR)/initramfs-root/tests/test_read
	cp $(BUILD_DIR)/tests/raw/test_stat.elf        $(BUILD_DIR)/initramfs-root/tests/test_stat
	cp $(BUILD_DIR)/tests/raw/test_mmap.elf        $(BUILD_DIR)/initramfs-root/tests/test_mmap
	cp $(BUILD_DIR)/tests/raw/test_mmap_file.elf   $(BUILD_DIR)/initramfs-root/tests/test_mmap_file
	cp $(BUILD_DIR)/tests/raw/test_process.elf     $(BUILD_DIR)/initramfs-root/tests/test_process
	cp $(BUILD_DIR)/tests/raw/test_clock.elf       $(BUILD_DIR)/initramfs-root/tests/test_clock
	cp $(BUILD_DIR)/tests/raw/test_getrandom.elf   $(BUILD_DIR)/initramfs-root/tests/test_getrandom
	cp $(BUILD_DIR)/tests/raw/test_all.elf         $(BUILD_DIR)/initramfs-root/tests/test_all
	# Copy fake interpreter and dynamic binary for PT_INTERP test
	cp tests/userspace/dynamic-musl/fake_interp $(BUILD_DIR)/initramfs-root/tests/fake_interp
	cp tests/userspace/dynamic-musl/dynamic_binary $(BUILD_DIR)/initramfs-root/tests/dynamic_binary
	# Copy dynamic musl hello world and the patched dynamic test binaries
	cp $(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic $(BUILD_DIR)/initramfs-root/bin/hello_dynamic
	cp $(BUILD_DIR)/tests/userspace/dynamic-musl/missing_interp $(BUILD_DIR)/initramfs-root/tests/missing_interp
	cp $(BUILD_DIR)/tests/userspace/dynamic-musl/invalid_interp $(BUILD_DIR)/initramfs-root/tests/invalid_interp
	# Copy musl dynamic linker to initramfs /lib/ld-musl-x86_64.so.1
	cp tests/userspace/dynamic-musl/ld-musl-x86_64.so.1 $(BUILD_DIR)/initramfs-root/lib/ld-musl-x86_64.so.1
	python3 scripts/make_initramfs.py $(BUILD_DIR)/initramfs-root $(BUILD_DIR)/initramfs.tar || python scripts/make_initramfs.py $(BUILD_DIR)/initramfs-root $(BUILD_DIR)/initramfs.tar

$(BUILD_DIR)/user/init.elf: user/init/init.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/init/init.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/sh.elf: user/shell/sh.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/shell/sh.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/test_read_kernel.elf: user/tests/test_read_kernel.S user/user.ld
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $(BUILD_DIR)/user/test_read_kernel.o
	$(LD) -T user/user.ld -nostdlib -static $(BUILD_DIR)/user/test_read_kernel.o -o $@

$(BUILD_DIR)/user/test_privileged.elf: user/tests/test_privileged.S user/user.ld
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $(BUILD_DIR)/user/test_privileged.o
	$(LD) -T user/user.ld -nostdlib -static $(BUILD_DIR)/user/test_privileged.o -o $@

# ----------------------------------------------------------------
# Raw-syscall no-libc test programs
# ----------------------------------------------------------------
# Pattern rule: build each .c under tests/userspace/raw-syscall/
# as a standalone ELF with no libc and no CRT.
$(BUILD_DIR)/tests/raw/%.elf: tests/userspace/raw-syscall/%.c user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(RAW_CFLAGS) -T user/user.ld -nostdlib -static $< -o $@

userspace-tests: $(RAWSYSCALL_TESTS)
	@echo "Raw-syscall tests built: $(RAWSYSCALL_TESTS)"

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
	$(LIMINE) bios-install $(ISO)

run: $(ISO)
	@mkdir -p $(BUILD_DIR)
	qemu-system-x86_64 -M q35 -m 64M -cdrom $(ISO) \
		-serial file:$(SERIAL_LOG) -debugcon stdio -no-reboot -no-shutdown \
		-netdev user,id=n0,hostfwd=tcp::8080-:80 -device virtio-net-pci,netdev=n0

# Helper: boot the kernel once with a 25s timeout and capture the serial log.
# Returns 0 on timeout-killed QEMU (normal) and writes the log file.
define boot-and-capture
@mkdir -p $(BUILD_DIR)
@rm -f $(1)
@(timeout 25s qemu-system-x86_64 -M q35 -m 64M -cdrom $(ISO) \
	-serial file:$(1) -debugcon stdio -no-reboot -no-shutdown \
	-netdev user,id=n0,hostfwd=tcp::8080-:80 -device virtio-net-pci,netdev=n0 \
	> /dev/null 2>&1 || test $$? -eq 124)
endef

# Standard boot: every success marker must appear, and no exception/panic.
verify-boot:
	$(MAKE) clean
	$(MAKE) iso
	$(call boot-and-capture,$(SERIAL_LOG))
	@grep -q "VMM: address-space self-test passed" $(SERIAL_LOG)
	@grep -q "VMM: negative self-test passed" $(SERIAL_LOG)
	@grep -q "VMM: permission self-test passed" $(SERIAL_LOG)
	@grep -q "Heap: self-test passed" $(SERIAL_LOG)
	@grep -q "Sched: fair-share test passed" $(SERIAL_LOG)
	@grep -q "Sched: priority test passed" $(SERIAL_LOG)
	@grep -q "Sched: sleep test passed" $(SERIAL_LOG)
	@grep -q "Sched: orphan reparenting test passed" $(SERIAL_LOG)
	@grep -q "Sched: multi-thread test passed" $(SERIAL_LOG)
	@grep -q "Sched: wait_any/specific/no-children/zombie test passed" $(SERIAL_LOG)
	@grep -q "Sched: Phase 7 scheduler self-test passed" $(SERIAL_LOG)
	@grep -q "Syscall: Phase 8 self-test passed" $(SERIAL_LOG)
	@grep -q "Phase 9 & 10 tests passed successfully" $(SERIAL_LOG)
	@grep -q "Net: Initialized network protocol stack" $(SERIAL_LOG)
	@grep -q "UDP Echo Server listening on port 9999" $(SERIAL_LOG)
	@grep -q "TCP HTTP Server listening on port 80" $(SERIAL_LOG)
	@grep -q "Test 22: PASSED" $(SERIAL_LOG)
	@grep -q "Hello from dynamic musl!" $(SERIAL_LOG)
	@! grep -q "CPU exception" $(SERIAL_LOG)
	@! grep -q "KERNEL PANIC" $(SERIAL_LOG)
	@echo "Boot verification passed"

# Negative boot: build with TEST=vmm-fault and expect an early CPU exception.
verify-boot-vmm:
	$(MAKE) clean
	$(MAKE) iso TEST=vmm-fault
	$(call boot-and-capture,$(SERIAL_LOG_VMM))
	@grep -q "CPU exception" $(SERIAL_LOG_VMM)
	@grep -q "Vector: 14" $(SERIAL_LOG_VMM)
	@echo "VMM fault boot verification passed"

# Negative boot: build with TEST=heap-panic and expect a KERNEL PANIC.
verify-boot-heap:
	$(MAKE) clean
	$(MAKE) iso TEST=heap-panic
	$(call boot-and-capture,$(SERIAL_LOG_HEAP))
	@grep -q "KERNEL PANIC" $(SERIAL_LOG_HEAP)
	@grep -q "Heap:" $(SERIAL_LOG_HEAP)
	@echo "Heap panic boot verification passed"

# Run every boot verification mode in sequence. Restores the default build
# at the end so the regular `make` target is unchanged.
verify-boot-all: verify-boot verify-boot-vmm verify-boot-heap
	@echo "All boot verifications passed"

debug-gdb: $(ISO)
	qemu-system-x86_64 -M q35 -m 64M -cdrom $(ISO) \
		-serial file:$(SERIAL_LOG) -S -s -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
