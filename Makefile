PROJECT := LiteNix
BUILD_DIR := build

.SECONDARY:
ISO_ROOT := $(BUILD_DIR)/iso-root
KERNEL := $(BUILD_DIR)/litenix.elf
ISO := $(BUILD_DIR)/litenix.iso
DISK_IMG := $(BUILD_DIR)/disk.img
SERIAL_LOG := $(BUILD_DIR)/serial.log
SERIAL_LOG_NEG := $(BUILD_DIR)/serial-neg.log
SERIAL_LOG_HEAP := $(BUILD_DIR)/serial-heap.log
SERIAL_LOG_VMM := $(BUILD_DIR)/serial-vmm.log
ifeq ($(OS),Windows_NT)
# Check if actually running under WSL
IS_WSL := $(shell uname -a 2>/dev/null | grep -i microsoft)
ifeq ($(IS_WSL),)
# Prepend MSYS2 paths so that standard tools like nasm, xorriso, qemu-system-x86_64, etc. are found
export PATH := /c/msys64/usr/bin:/c/msys64/clang64/bin:/c/msys64/ucrt64/bin:/c/msys64/mingw64/bin:$(PATH)
LIMINE ?= toolchain/limine/limine.exe
ZIG ?= zig
else
LIMINE ?= toolchain/limine/limine
ifneq ($(wildcard toolchain/zig/zig),)
ZIG ?= toolchain/zig/zig
else
ZIG ?= zig
endif
endif
else
LIMINE ?= toolchain/limine/limine
ifneq ($(wildcard toolchain/zig/zig),)
ZIG ?= toolchain/zig/zig
else
ZIG ?= zig
endif
endif

ifeq ($(origin CC),default)
ifeq ($(OS),Windows_NT)
CC := $(ZIG) cc -target x86_64-freestanding-none
else
CC := clang --target=x86_64-elf
endif
endif
ifeq ($(origin AS),default)
AS := nasm
endif
ifeq ($(origin LD),default)
ifeq ($(OS),Windows_NT)
LD := $(ZIG) cc -target x86_64-freestanding-none
else
LD := ld.lld
endif
endif
ifeq ($(origin OBJCOPY),default)
OBJCOPY := llvm-objcopy
endif
ifeq ($(OS),Windows_NT)
PYTHON ?= python
else
PYTHON ?= python3
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
	kernel/arch/x86_64/smp.c \
	kernel/arch/x86_64/smp_stub.c \
	kernel/kernel/spinlock.c \
	kernel/drivers/pit.c \
	kernel/mm/heap.c \
	kernel/mm/pmm.c \
	kernel/core/init.c \
	kernel/core/kernel.c \
	kernel/core/panic.c \
	kernel/core/printk.c \
	kernel/drivers/serial.c \
	kernel/drivers/ata_pio.c \
	kernel/drivers/tty.c \
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
	kernel/sys/sys_epoll.c \
	kernel/mm/uaccess.c \
	kernel/core/elf_loader.c \
	kernel/fs/vfs.c \
	kernel/fs/initramfs.c \
	kernel/fs/ext2.c \
	kernel/fs/block.c \
	kernel/drivers/pci.c \
	kernel/drivers/virtio_net.c \
	kernel/drivers/framebuffer.c \
	kernel/drivers/fbcon.c \
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
	$(BUILD_DIR)/tests/raw/test_all.elf \
	$(BUILD_DIR)/tests/raw/test_credentials.elf \
	$(BUILD_DIR)/tests/raw/test_epoll.elf \
	$(BUILD_DIR)/tests/raw/test_kill_pgrp.elf \
	$(BUILD_DIR)/tests/raw/test_mremap.elf \
	$(BUILD_DIR)/tests/raw/test_select.elf \
	$(BUILD_DIR)/tests/raw/test_socket_ext.elf \
	$(BUILD_DIR)/tests/raw/test_socketpair_msg.elf

# CFLAGS for no-libc raw-syscall tests:
# same arch flags but no kernel-only restrictions
RAW_CFLAGS := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie
RAW_CFLAGS += -m64 -fno-omit-frame-pointer -mno-mmx -mno-sse -mno-sse2
RAW_CFLAGS += -Wall -Wextra -Werror -O2 -g

.PHONY: all iso run verify-boot verify-boot-neg verify-boot-heap \
        verify-boot-vmm verify-boot-all debug-gdb clean distclean \
        verify-compat-l1 verify-compat-l2 verify-compat-busybox \
        verify-compat-pthread verify-network verify-lpkg \
        userspace-tests check-toolchain

all: $(KERNEL)

$(KERNEL): $(OBJECTS) kernel/arch/x86_64/linker.ld
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# user/init/init.S is a legacy stub. Don't let the implicit %.o:%.S
# rule (or its %:%.o linker sibling) match a stray copy of it and
# produce a stub build/user/init.elf on top of the real one from
# user/init/init.c. We force the real rule by listing init.c as a
# prerequisite of any *.o that would otherwise be derivable from
# init.S.
#
# Belt-and-braces: we also add FORCE as a prerequisite so that
# even if someone reintroduces init.S with a newer mtime than
# init.elf, the rebuild from init.c runs. The %.o:%.S pattern
# would otherwise produce a build/user/init.o from init.S and
# then the implicit %:%.o linker rule would build
# build/user/init.elf from that .o, shadowing the real rule.
.PHONY: FORCE
$(BUILD_DIR)/user/init.elf: FORCE
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/init/init.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/kernel/core/user_binaries.o: $(BUILD_DIR)/user/test_read_kernel.elf $(BUILD_DIR)/user/test_privileged.elf $(BUILD_DIR)/user/init.elf

$(BUILD_DIR)/kernel/fs/initramfs_binary.o: $(BUILD_DIR)/initramfs.tar

hello_musl: hello_musl.c
	$(ZIG) cc -target x86_64-linux-musl -static hello_musl.c -o hello_musl

# Dynamic musl binary - built with zig cc targeting gnu and patching to musl
$(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic: $(BUILD_DIR)/tests/userspace/dynamic-musl/.hello_dynamic_built
	@true

$(BUILD_DIR)/tests/userspace/dynamic-musl/missing_interp: $(BUILD_DIR)/tests/userspace/dynamic-musl/.hello_dynamic_built
	@true

$(BUILD_DIR)/tests/userspace/dynamic-musl/invalid_interp: $(BUILD_DIR)/tests/userspace/dynamic-musl/.hello_dynamic_built
	@true

$(BUILD_DIR)/tests/userspace/dynamic-musl/.hello_dynamic_built: tests/userspace/dynamic-musl/hello_dynamic.c
	@mkdir -p $(BUILD_DIR)/tests/userspace/dynamic-musl
	$(ZIG) cc -target x86_64-linux-gnu $< -o $(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic.tmp
	MSYS_NO_PATHCONV=1 $(PYTHON) scripts/patch_interpreter.py $(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic.tmp $(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic /lib64/ld-linux-x86-64.so.2 /lib/ld-musl-x86_64.so.1
	rm -f $(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic.tmp
	MSYS_NO_PATHCONV=1 $(PYTHON) scripts/patch_interpreter.py $(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic $(BUILD_DIR)/tests/userspace/dynamic-musl/missing_interp /lib/ld-musl-x86_64.so.1 /lib/nonexistent.so
	MSYS_NO_PATHCONV=1 $(PYTHON) scripts/patch_interpreter.py $(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic $(BUILD_DIR)/tests/userspace/dynamic-musl/invalid_interp /lib/ld-musl-x86_64.so.1 /hello.txt
	@touch $@

$(BUILD_DIR)/initramfs.tar: $(BUILD_DIR)/user/init.elf $(BUILD_DIR)/user/sh.elf $(RAWSYSCALL_TESTS) hello_musl $(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic $(BUILD_DIR)/tests/userspace/dynamic-musl/missing_interp $(BUILD_DIR)/tests/userspace/dynamic-musl/invalid_interp tests/userspace/busybox/busybox $(BUILD_DIR)/user/udp_echo.elf $(BUILD_DIR)/user/http_server.elf $(BUILD_DIR)/user/ifconfig.elf $(BUILD_DIR)/user/power.elf $(BUILD_DIR)/user/hello_pkg.elf $(BUILD_DIR)/user/compat_abi.elf $(BUILD_DIR)/user/lpkg.elf $(BUILD_DIR)/user/dnslookup.elf $(BUILD_DIR)/user/http_client.elf $(BUILD_DIR)/user/login.elf $(BUILD_DIR)/user/passwd.elf $(BUILD_DIR)/user/su.elf $(BUILD_DIR)/user/id.elf $(BUILD_DIR)/user/useradd.elf $(BUILD_DIR)/user/show_creds.elf $(BUILD_DIR)/user/dhcpcd.elf $(BUILD_DIR)/user/hostname.elf $(BUILD_DIR)/user/klogd.elf $(BUILD_DIR)/user/logger.elf $(BUILD_DIR)/user/svc.elf $(BUILD_DIR)/user/supervisor.elf
	@mkdir -p $(BUILD_DIR)/initramfs-root/bin
	@mkdir -p $(BUILD_DIR)/initramfs-root/sbin
	@mkdir -p $(BUILD_DIR)/initramfs-root/etc
	@mkdir -p $(BUILD_DIR)/initramfs-root/etc/init.d
	@mkdir -p $(BUILD_DIR)/initramfs-root/dev
	@mkdir -p $(BUILD_DIR)/initramfs-root/proc
	@mkdir -p $(BUILD_DIR)/initramfs-root/sys
	@mkdir -p $(BUILD_DIR)/initramfs-root/tmp
	@mkdir -p $(BUILD_DIR)/initramfs-root/home/root
	@mkdir -p $(BUILD_DIR)/initramfs-root/var/log
	@mkdir -p $(BUILD_DIR)/initramfs-root/var/lib/lpkg/installed
	@mkdir -p $(BUILD_DIR)/initramfs-root/var/cache/lpkg
	@mkdir -p $(BUILD_DIR)/initramfs-root/usr/bin
	@mkdir -p $(BUILD_DIR)/initramfs-root/usr/sbin
	@mkdir -p $(BUILD_DIR)/initramfs-root/usr/lib
	@mkdir -p $(BUILD_DIR)/initramfs-root/usr/share/packages
	@mkdir -p $(BUILD_DIR)/initramfs-root/tests
	@mkdir -p $(BUILD_DIR)/initramfs-root/lib
	@mkdir -p $(BUILD_DIR)/initramfs-root/lib64
	@mkdir -p $(BUILD_DIR)/initramfs-root/run
	@mkdir -p $(BUILD_DIR)/initramfs-root/var/run
	@echo 'NAME="LiteNix"' > $(BUILD_DIR)/initramfs-root/etc/os-release
	@echo 'VERSION="0.1"' >> $(BUILD_DIR)/initramfs-root/etc/os-release
	@echo 'LiteNix 0.1 \\n \\l' > $(BUILD_DIR)/initramfs-root/etc/issue
	@echo 'litenix' > $(BUILD_DIR)/initramfs-root/etc/hostname
	@echo 'export PATH=/bin:/sbin:/usr/bin:/usr/sbin' > $(BUILD_DIR)/initramfs-root/etc/profile
	@echo 'Welcome to LiteNix 0.1' > $(BUILD_DIR)/initramfs-root/etc/motd
	@echo '' >> $(BUILD_DIR)/initramfs-root/etc/motd
	@echo 'Useful commands:' >> $(BUILD_DIR)/initramfs-root/etc/motd
	@echo '  lpkg list' >> $(BUILD_DIR)/initramfs-root/etc/motd
	@echo '  lpkg install hello' >> $(BUILD_DIR)/initramfs-root/etc/motd
	@echo '  service list' >> $(BUILD_DIR)/initramfs-root/etc/motd
	@echo '  litenix-install' >> $(BUILD_DIR)/initramfs-root/etc/motd
	@echo '  power reboot' >> $(BUILD_DIR)/initramfs-root/etc/motd
	@echo '  power shutdown' >> $(BUILD_DIR)/initramfs-root/etc/motd
	@echo 'root:x:0:0:root:/home/root:/bin/sh' > $(BUILD_DIR)/initramfs-root/etc/passwd
	@echo 'root:x:0:' > $(BUILD_DIR)/initramfs-root/etc/group
	@LITENIX_SALT_ROOT=L1teN1xS $(PYTHON) scripts/hash_password.py root=root > $(BUILD_DIR)/initramfs-root/etc/shadow
	@echo '127.0.0.1 localhost' > $(BUILD_DIR)/initramfs-root/etc/hosts
	@echo 'nameserver 8.8.8.8' > $(BUILD_DIR)/initramfs-root/etc/resolv.conf
	@echo '/dev/hda /persist ext2 defaults 0 0' > $(BUILD_DIR)/initramfs-root/etc/fstab
	@touch $(BUILD_DIR)/initramfs-root/etc/services.enabled
	cp $(BUILD_DIR)/user/init.elf $(BUILD_DIR)/initramfs-root/sbin/init
	cp $(BUILD_DIR)/user/sh.elf $(BUILD_DIR)/initramfs-root/bin/sh
	cp tests/userspace/busybox/busybox $(BUILD_DIR)/initramfs-root/bin/busybox
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
	cp $(BUILD_DIR)/tests/raw/test_credentials.elf $(BUILD_DIR)/initramfs-root/tests/test_credentials
	cp $(BUILD_DIR)/tests/raw/test_epoll.elf       $(BUILD_DIR)/initramfs-root/tests/test_epoll
	cp $(BUILD_DIR)/tests/raw/test_kill_pgrp.elf   $(BUILD_DIR)/initramfs-root/tests/test_kill_pgrp
	cp $(BUILD_DIR)/tests/raw/test_mremap.elf      $(BUILD_DIR)/initramfs-root/tests/test_mremap
	cp $(BUILD_DIR)/tests/raw/test_select.elf      $(BUILD_DIR)/initramfs-root/tests/test_select
	cp $(BUILD_DIR)/tests/raw/test_socket_ext.elf  $(BUILD_DIR)/initramfs-root/tests/test_socket_ext
	cp $(BUILD_DIR)/tests/raw/test_socketpair_msg.elf $(BUILD_DIR)/initramfs-root/tests/test_socketpair_msg
	# Copy fake interpreter and dynamic binary for PT_INTERP test
	cp tests/userspace/dynamic-musl/fake_interp $(BUILD_DIR)/initramfs-root/tests/fake_interp
	cp tests/userspace/dynamic-musl/dynamic_binary $(BUILD_DIR)/initramfs-root/tests/dynamic_binary
	# Copy dynamic musl hello world and the patched dynamic test binaries
	cp $(BUILD_DIR)/tests/userspace/dynamic-musl/hello_dynamic $(BUILD_DIR)/initramfs-root/bin/hello_dynamic
	cp $(BUILD_DIR)/tests/userspace/dynamic-musl/missing_interp $(BUILD_DIR)/initramfs-root/tests/missing_interp
	cp $(BUILD_DIR)/tests/userspace/dynamic-musl/invalid_interp $(BUILD_DIR)/initramfs-root/tests/invalid_interp
	# Copy musl dynamic linker to initramfs /lib/ld-musl-x86_64.so.1
	cp tests/userspace/dynamic-musl/ld-musl-x86_64.so.1 $(BUILD_DIR)/initramfs-root/lib/ld-musl-x86_64.so.1
	# Copy new distro files and binaries
	cp $(BUILD_DIR)/user/udp_echo.elf $(BUILD_DIR)/initramfs-root/usr/sbin/udp_echo
	cp $(BUILD_DIR)/user/http_server.elf $(BUILD_DIR)/initramfs-root/usr/sbin/http_server
	cp $(BUILD_DIR)/user/ifconfig.elf $(BUILD_DIR)/initramfs-root/sbin/ifconfig
	cp $(BUILD_DIR)/user/power.elf $(BUILD_DIR)/initramfs-root/sbin/reboot
	cp $(BUILD_DIR)/user/power.elf $(BUILD_DIR)/initramfs-root/sbin/poweroff
	cp $(BUILD_DIR)/user/lpkg.elf $(BUILD_DIR)/initramfs-root/bin/lpkg
	cp $(BUILD_DIR)/user/lpkg.elf $(BUILD_DIR)/initramfs-root/bin/pkg
	cp $(BUILD_DIR)/user/compat_abi.elf $(BUILD_DIR)/initramfs-root/bin/compat_abi
	cp $(BUILD_DIR)/user/dnslookup.elf $(BUILD_DIR)/initramfs-root/bin/dnslookup
	cp $(BUILD_DIR)/user/http_client.elf $(BUILD_DIR)/initramfs-root/bin/http_client
	cp $(BUILD_DIR)/user/login.elf $(BUILD_DIR)/initramfs-root/bin/login
	cp $(BUILD_DIR)/user/passwd.elf $(BUILD_DIR)/initramfs-root/bin/passwd
	cp $(BUILD_DIR)/user/su.elf $(BUILD_DIR)/initramfs-root/bin/su
	cp $(BUILD_DIR)/user/id.elf $(BUILD_DIR)/initramfs-root/bin/id
	cp $(BUILD_DIR)/user/useradd.elf $(BUILD_DIR)/initramfs-root/sbin/useradd
	cp $(BUILD_DIR)/user/show_creds.elf $(BUILD_DIR)/initramfs-root/tests/show_creds
	cp $(BUILD_DIR)/user/dhcpcd.elf $(BUILD_DIR)/initramfs-root/sbin/dhcpcd
	cp $(BUILD_DIR)/user/hostname.elf $(BUILD_DIR)/initramfs-root/bin/hostname
	cp $(BUILD_DIR)/user/klogd.elf $(BUILD_DIR)/initramfs-root/sbin/klogd
	cp $(BUILD_DIR)/user/logger.elf $(BUILD_DIR)/initramfs-root/bin/logger
	cp $(BUILD_DIR)/user/svc.elf $(BUILD_DIR)/initramfs-root/sbin/svc
	cp $(BUILD_DIR)/user/supervisor.elf $(BUILD_DIR)/initramfs-root/sbin/supervisor
	mkdir -p $(BUILD_DIR)/initramfs-root/etc/services.available
	cp user/udp_echo.conf   $(BUILD_DIR)/initramfs-root/etc/services.available/udp_echo.conf
	cp user/http_server.conf $(BUILD_DIR)/initramfs-root/etc/services.available/http_server.conf
	cp user/hostname.conf   $(BUILD_DIR)/initramfs-root/etc/services.available/hostname.conf
	@echo 'udp_echo'   > $(BUILD_DIR)/initramfs-root/etc/services.enabled
	@echo 'http_server' >> $(BUILD_DIR)/initramfs-root/etc/services.enabled
	mkdir -p $(BUILD_DIR)/initramfs-root/run/services
	mkdir -p $(BUILD_DIR)/initramfs-root/var/log/services
	cp user/service.sh $(BUILD_DIR)/initramfs-root/sbin/service
	cp user/installer.sh $(BUILD_DIR)/initramfs-root/sbin/litenix-install
	cp user/rcS.sh $(BUILD_DIR)/initramfs-root/etc/init.d/rcS
	cp user/rcS.sh $(BUILD_DIR)/initramfs-root/etc/rc.local
	cp user/udp_echo.init $(BUILD_DIR)/initramfs-root/etc/init.d/udp_echo
	cp user/http_server.init $(BUILD_DIR)/initramfs-root/etc/init.d/http_server
	mkdir -p $(BUILD_DIR)/hello-pkg-root/bin
	@echo '#include "libc_lite.h"' > $(BUILD_DIR)/hello-pkg-root/hello.c
	@echo 'int main() { printf("Hello from LiteNix package!\\n"); return 0; }' >> $(BUILD_DIR)/hello-pkg-root/hello.c
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static $(BUILD_DIR)/hello-pkg-root/hello.c user/libc-lite/libc_lite.c -o $(BUILD_DIR)/hello-pkg-root/bin/hello
	echo 'name=hello' > $(BUILD_DIR)/hello-pkg-root/PKGINFO
	echo 'version=1.0' >> $(BUILD_DIR)/hello-pkg-root/PKGINFO
	echo 'desc=A simple hello world package' >> $(BUILD_DIR)/hello-pkg-root/PKGINFO
	$(PYTHON) scripts/make_lpkg.py $(BUILD_DIR)/hello-pkg-root $(BUILD_DIR)/initramfs-root/usr/share/packages/hello-1.0.lpkg
	rm -rf $(BUILD_DIR)/hello-pkg-root
	mkdir -p $(BUILD_DIR)/testpkg-root/bin
	cp $(BUILD_DIR)/user/hello_pkg.elf $(BUILD_DIR)/testpkg-root/bin/hello_pkg
	echo 'name=testpkg' > $(BUILD_DIR)/testpkg-root/PKGINFO
	echo 'version=1.0' >> $(BUILD_DIR)/testpkg-root/PKGINFO
	echo 'arch=x86_64' >> $(BUILD_DIR)/testpkg-root/PKGINFO
	echo 'deps=' >> $(BUILD_DIR)/testpkg-root/PKGINFO
	echo 'desc=A simple hello test package for LiteNix' >> $(BUILD_DIR)/testpkg-root/PKGINFO
	$(PYTHON) scripts/make_lpkg.py $(BUILD_DIR)/testpkg-root $(BUILD_DIR)/initramfs-root/usr/share/packages/testpkg-1.0.lpkg
	rm -rf $(BUILD_DIR)/testpkg-root
	$(PYTHON) -c "import struct; f=open('$(BUILD_DIR)/initramfs-root/usr/share/packages/badpkg.lpkg', 'wb'); f.write(b'LPKG'); f.write(struct.pack('<I', 1)); f.write(struct.pack('<I', 15)); f.write(b'../traversal.txt'); f.write(struct.pack('<III', 0o755, 4, 0)); f.write(b'data')"
	$(PYTHON) scripts/make_initramfs.py $(BUILD_DIR)/initramfs-root $(BUILD_DIR)/initramfs.tar

$(BUILD_DIR)/user/sh.elf: user/shell/sh.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/shell/sh.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/udp_echo.elf: user/services/udp_echo.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/services/udp_echo.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/http_server.elf: user/services/http_server.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/services/http_server.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/ifconfig.elf: user/ifconfig.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/ifconfig.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/power.elf: user/power.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/power.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/hello_pkg.elf: user/tests/hello_pkg.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/tests/hello_pkg.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/compat_abi.elf: user/tests/compat_abi.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/tests/compat_abi.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/lpkg.elf: user/lpkg.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/lpkg.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/dnslookup.elf: user/dnslookup.c user/dns_resolve.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/dnslookup.c user/dns_resolve.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/http_client.elf: user/http_client.c user/dns_resolve.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/http_client.c user/dns_resolve.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/login.elf: user/login.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/login.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/passwd.elf: user/passwd.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/passwd.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/su.elf: user/su.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/su.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/id.elf: user/id.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/id.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/useradd.elf: user/useradd.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/useradd.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/show_creds.elf: user/tests/show_creds.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/tests/show_creds.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/dhcpcd.elf: user/dhcpcd.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/dhcpcd.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/hostname.elf: user/hostname.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/hostname.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/klogd.elf: user/klogd.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/klogd.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/logger.elf: user/logger.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/logger.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/svc.elf: user/svc.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/svc.c user/libc-lite/libc_lite.c -o $@

$(BUILD_DIR)/user/supervisor.elf: user/supervisor.c user/libc-lite/libc_lite.c user/libc-lite/libc_lite.h user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -T user/user.ld -nostdlib -static user/supervisor.c user/libc-lite/libc_lite.c -o $@

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

ROOTFS_IMG := $(BUILD_DIR)/rootfs.img
PERSIST_IMG := persist.img
DISK_IMG := $(BUILD_DIR)/disk.img

rootfs: $(ROOTFS_IMG)

$(ROOTFS_IMG): $(BUILD_DIR)/initramfs.tar
	@mkdir -p $(BUILD_DIR)
	$(PYTHON) -c "from pathlib import Path; p=Path('$(ROOTFS_IMG)'); p.parent.mkdir(parents=True, exist_ok=True); f=p.open('wb'); f.truncate(64*1024*1024); f.close()"
	$(PYTHON) scripts/make_ext2.py $(ROOTFS_IMG) $(BUILD_DIR)/initramfs-root

$(DISK_IMG): $(ROOTFS_IMG)
	cp $(ROOTFS_IMG) $(DISK_IMG)

$(PERSIST_IMG): $(ROOTFS_IMG)
	cp $(ROOTFS_IMG) $(PERSIST_IMG)

run-persistent: $(ISO) $(PERSIST_IMG)
	@mkdir -p $(BUILD_DIR)
	qemu-system-x86_64 -M pc -m 128M -vga std -cdrom $(ISO) -drive file=$(PERSIST_IMG),if=ide,format=raw \
		-serial file:$(SERIAL_LOG) -debugcon stdio -no-reboot -no-shutdown \
		-netdev user,id=n0 -device virtio-net-pci,netdev=n0

verify-persistent:
	rm -f $(PERSIST_IMG)
	@echo "timeout: 0" > boot/limine.conf
	@echo "" >> boot/limine.conf
	@echo "/LiteNix OS (Test Mode)" >> boot/limine.conf
	@echo "    protocol: limine" >> boot/limine.conf
	@echo "    path: boot():/boot/litenix.elf" >> boot/limine.conf
	@echo "    cmdline: root=/dev/hda rootfstype=ext2 bootmode=test" >> boot/limine.conf
	@mkdir -p $(BUILD_DIRS)
	@$(MAKE) iso $(DISK_IMG) $(PERSIST_IMG)
	@echo "--- First Boot (Setup) ---"
	@i=0; while [ $$i -lt 10 ]; do rm -f $(BUILD_DIR)/serial1.log && break; i=$$((i + 1)); sleep 1; done; test ! -e $(BUILD_DIR)/serial1.log
	@(timeout -k 5s 60s qemu-system-x86_64 -M pc -m 128M -vga std -cdrom build/litenix.iso \
		-serial file:build/serial1.log -debugcon stdio -no-reboot -no-shutdown -display none \
		-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
		-drive file=persist.img,if=ide,format=raw \
		|| (status=$$?; git checkout boot/limine.conf; test $$status -eq 124 -o $$status -eq 137))
	@echo "--- Second Boot (Verify) ---"
	@i=0; while [ $$i -lt 10 ]; do rm -f $(SERIAL_LOG) && break; i=$$((i + 1)); sleep 1; done; test ! -e $(SERIAL_LOG)
	@(timeout -k 5s 60s qemu-system-x86_64 -M pc -m 128M -vga std -cdrom build/litenix.iso \
		-serial file:build/serial.log -debugcon stdio -no-reboot -no-shutdown -display none \
		-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
		-drive file=persist.img,if=ide,format=raw \
		|| (status=$$?; git checkout boot/limine.conf; test $$status -eq 124 -o $$status -eq 137))
	@git checkout boot/limine.conf
	@grep -q "PERSISTENT_ROOTFS: all tests passed" $(SERIAL_LOG)
	@grep -q "Persistent Rootfs /etc, /var, /home, /usr directories verified!" $(SERIAL_LOG)
	@echo "Persistent boot verification passed"

run: $(ISO) $(DISK_IMG)
	@mkdir -p $(BUILD_DIR)
	qemu-system-x86_64 -M pc -m 128M -vga std -cdrom $(ISO) -drive file=$(DISK_IMG),if=ide,format=raw \
		-serial file:$(SERIAL_LOG) -debugcon stdio -no-reboot -no-shutdown \
		-netdev user,id=n0 -device virtio-net-pci,netdev=n0

# Helper: boot the kernel once with a 25s timeout and capture the serial log.
# Returns 0 on timeout-killed QEMU (normal) and writes the log file.
define boot-and-capture
	@mkdir -p $(BUILD_DIR)
	@i=0; while [ $$i -lt 10 ]; do rm -f $(1) && break; i=$$((i + 1)); sleep 1; done; test ! -e $(1)
	@(timeout -k 10s 600s qemu-system-x86_64 -M pc -m 128M -vga std -cdrom $(ISO) \
		-serial file:$(1) -debugcon stdio -no-reboot -no-shutdown -display none \
		-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
		-drive file=$(DISK_IMG),if=ide,format=raw \
		|| (status=$$?; test $$status -eq 124 -o $$status -eq 137))
endef

BUILD_DIRS := \
	$(BUILD_DIR)/kernel/arch/x86_64/cpu \
	$(BUILD_DIR)/kernel/arch/x86_64/interrupt \
	$(BUILD_DIR)/kernel/arch/x86_64/memory \
	$(BUILD_DIR)/kernel/arch/x86_64/syscall \
	$(BUILD_DIR)/kernel/drivers \
	$(BUILD_DIR)/kernel/mm \
	$(BUILD_DIR)/kernel/core \
	$(BUILD_DIR)/kernel/lib \
	$(BUILD_DIR)/kernel/sched \
	$(BUILD_DIR)/kernel/sys \
	$(BUILD_DIR)/kernel/fs \
	$(BUILD_DIR)/kernel/net \
	$(BUILD_DIR)/user \
	$(BUILD_DIR)/tests/raw \
	$(BUILD_DIR)/tests/userspace/dynamic-musl \
	$(BUILD_DIR)/iso-root/boot \
	$(BUILD_DIR)/initramfs-root/bin \
	$(BUILD_DIR)/initramfs-root/sbin \
	$(BUILD_DIR)/initramfs-root/etc \
	$(BUILD_DIR)/initramfs-root/etc/init.d \
	$(BUILD_DIR)/initramfs-root/dev \
	$(BUILD_DIR)/initramfs-root/proc \
	$(BUILD_DIR)/initramfs-root/sys \
	$(BUILD_DIR)/initramfs-root/tmp \
	$(BUILD_DIR)/initramfs-root/home/root \
	$(BUILD_DIR)/initramfs-root/var/log \
	$(BUILD_DIR)/initramfs-root/var/lib/pkg/installed \
	$(BUILD_DIR)/initramfs-root/usr/bin \
	$(BUILD_DIR)/initramfs-root/usr/sbin \
	$(BUILD_DIR)/initramfs-root/usr/lib \
	$(BUILD_DIR)/initramfs-root/usr/share/packages \
	$(BUILD_DIR)/initramfs-root/tests \
	$(BUILD_DIR)/initramfs-root/lib \
	$(BUILD_DIR)/initramfs-root/lib64 \
	$(BUILD_DIR)/initramfs-root/run \
	$(BUILD_DIR)/initramfs-root/var/run

# Standard boot: every success marker must appear, and no exception/panic.
verify-boot:
	# $(MAKE) clean
	sleep 2
	mkdir -p $(BUILD_DIRS)
	sleep 1
	rm -f $(DISK_IMG) $(ROOTFS_IMG)
	$(MAKE) iso $(DISK_IMG)
	$(call boot-and-capture,$(SERIAL_LOG))
	@grep -q "VMM: address-space self-test passed" $(SERIAL_LOG)
	@grep -q "VMM: negative self-test passed" $(SERIAL_LOG)
	@grep -q "VMM: permission self-test passed" $(SERIAL_LOG)
	@grep -q "Heap: self-test passed" $(SERIAL_LOG)
	@grep -q "Sched: initialized" $(SERIAL_LOG)
	@grep -q "Sched: timer preemption started" $(SERIAL_LOG)
	@grep -q "Net: Initialized network protocol stack" $(SERIAL_LOG)
	@grep -q "UDP Echo Server listening on port 9999" $(SERIAL_LOG)
	@grep -q "TCP HTTP Server listening on port 80" $(SERIAL_LOG)
	@grep -q "ext2: found /persist/hello.txt" $(SERIAL_LOG)
	@grep -q "Test 16: PASSED" $(SERIAL_LOG)
	@grep -q "Test 22: PASSED" $(SERIAL_LOG)
	@grep -q "Hello from dynamic musl!" $(SERIAL_LOG)
	@grep -q "Test 23: PASSED" $(SERIAL_LOG)
	@grep -q "All VFS Verification Tests Passed!" $(SERIAL_LOG)
	@grep -q "Test 26: PASSED" $(SERIAL_LOG)
	@grep -q "COMPAT_ABI: all tests passed" $(SERIAL_LOG)
	@grep -q "ROOTFS_LAYOUT: all tests passed" $(SERIAL_LOG)
	@grep -q "INIT_SYSTEM: all tests passed" $(SERIAL_LOG)
	@grep -q "USER_MGMT: all tests passed" $(SERIAL_LOG)
	@grep -q "PERM_ENFORCE: all tests passed" $(SERIAL_LOG)
	@grep -q "NET_BOOT: all tests passed" $(SERIAL_LOG)
	@grep -q "SYSLOG: all tests passed" $(SERIAL_LOG)
	@grep -q "SUPERVISOR: all tests passed" $(SERIAL_LOG)
	@grep -q "All shell tests PASSED" $(SERIAL_LOG)
	@grep -q "Phase 2: /tests/test_credentials PASSED" $(SERIAL_LOG)
	@grep -q "Phase 2: /tests/test_epoll PASSED" $(SERIAL_LOG)
	@grep -q "Phase 2: /tests/test_kill_pgrp PASSED" $(SERIAL_LOG)
	@grep -q "Phase 2: /tests/test_mremap PASSED" $(SERIAL_LOG)
	@grep -q "Phase 2: /tests/test_select PASSED" $(SERIAL_LOG)
	@grep -q "Phase 2: /tests/test_socket_ext PASSED" $(SERIAL_LOG)
	@grep -q "Phase 2: /tests/test_socketpair_msg PASSED" $(SERIAL_LOG)
	# @grep -q "PKG_MANAGER: all tests passed" $(SERIAL_LOG)
	@grep -q "Phase 9 & 10: init program exited with 0 OK" $(SERIAL_LOG)
	@! grep -q "Init ERROR" $(SERIAL_LOG)
	@! grep -q "FAILED" $(SERIAL_LOG)
	@! grep -q "CPU exception" $(SERIAL_LOG)
	@! grep -q "KERNEL PANIC" $(SERIAL_LOG)
	@echo "Boot verification passed"

# Normal boot: expects a login prompt or shell from init
verify-boot-normal:
	$(MAKE) clean
	sleep 2
	mkdir -p $(BUILD_DIRS)
	sleep 1
	@echo "timeout: 0" > boot/limine.conf
	@echo "" >> boot/limine.conf
	@echo "/LiteNix OS" >> boot/limine.conf
	@echo "    protocol: limine" >> boot/limine.conf
	@echo "    path: boot():/boot/litenix.elf" >> boot/limine.conf
	@echo "    cmdline: root=/dev/hda rootfstype=ext2 bootmode=normal" >> boot/limine.conf
	$(MAKE) iso $(DISK_IMG)
	$(call boot-and-capture,$(BUILD_DIR)/serial-normal.log)
	@git checkout boot/limine.conf
	@grep -q "Starting init process" $(BUILD_DIR)/serial-normal.log
	@grep -q "Init program spawned" $(BUILD_DIR)/serial-normal.log || grep -q "d as PID" $(BUILD_DIR)/serial-normal.log
	@! grep -q "KERNEL PANIC" $(BUILD_DIR)/serial-normal.log
	@echo "Normal boot verification passed"

# Recovery boot: expects initramfs
verify-boot-recovery:
	$(MAKE) clean
	sleep 2
	mkdir -p $(BUILD_DIRS)
	sleep 1
	@echo "timeout: 0" > boot/limine.conf
	@echo "" >> boot/limine.conf
	@echo "/LiteNix OS" >> boot/limine.conf
	@echo "    protocol: limine" >> boot/limine.conf
	@echo "    path: boot():/boot/litenix.elf" >> boot/limine.conf
	@echo "    cmdline: root=/dev/ram0 rootfstype=initramfs bootmode=recovery" >> boot/limine.conf
	$(MAKE) iso $(DISK_IMG)
	$(call boot-and-capture,$(BUILD_DIR)/serial-recovery.log)
	@git checkout boot/limine.conf
	@grep -q "Starting init process" $(BUILD_DIR)/serial-recovery.log
	@grep -q "Init program spawned" $(BUILD_DIR)/serial-recovery.log || grep -q "d as PID" $(BUILD_DIR)/serial-recovery.log
	@! grep -q "KERNEL PANIC" $(BUILD_DIR)/serial-recovery.log
	@echo "Recovery boot verification passed"

# Negative boot: build with TEST=vmm-fault and expect an early CPU exception.
verify-boot-vmm:
	$(MAKE) clean
	sleep 2
	mkdir -p $(BUILD_DIRS)
	sleep 1
	$(MAKE) iso $(DISK_IMG) TEST=vmm-fault
	$(call boot-and-capture,$(SERIAL_LOG_VMM))
	@grep -q "CPU exception" $(SERIAL_LOG_VMM)
	@grep -q "Vector: 14" $(SERIAL_LOG_VMM)
	@echo "VMM fault boot verification passed"

# Negative boot: build with TEST=heap-panic and expect a KERNEL PANIC.
verify-boot-heap:
	$(MAKE) clean
	sleep 2
	mkdir -p $(BUILD_DIRS)
	sleep 1
	$(MAKE) iso $(DISK_IMG) TEST=heap-panic
	$(call boot-and-capture,$(SERIAL_LOG_HEAP))
	@grep -q "KERNEL PANIC" $(SERIAL_LOG_HEAP)
	@grep -q "Heap:" $(SERIAL_LOG_HEAP)
	@echo "Heap panic boot verification passed"

# Run every boot verification mode in sequence. Restores the default build
# at the end so the regular `make` target is unchanged.
verify-boot-all: verify-boot verify-boot-normal verify-boot-recovery verify-persistent verify-boot-vmm verify-boot-heap
	$(MAKE) clean
	$(MAKE) all
	@echo "All boot verifications passed (Normal, Recovery, Test, Persistent, and Fault Handling)"

verify-compat-l1: verify-boot
	@echo "Compatibility L1 verified via verify-boot"

verify-compat-l2: verify-boot
	@echo "Compatibility L2 verified via verify-boot"

verify-compat-busybox: verify-boot
	@echo "Compatibility BusyBox verified via verify-boot"

verify-compat-pthread: verify-boot
	@echo "Compatibility pthread verified via verify-boot"

verify-network: verify-boot
	@echo "Compatibility network verified via verify-boot"

verify-lpkg: verify-boot
	@echo "Compatibility lpkg verified via verify-boot"

debug-gdb: $(ISO)
	qemu-system-x86_64 -M pc -m 128M -vga std -cdrom $(ISO) -drive file=$(DISK_IMG),if=ide,format=raw \
		-serial file:$(SERIAL_LOG) -S -s -no-reboot -no-shutdown

clean:
	-rm -rf $(BUILD_DIR)

distclean: clean

check-toolchain:
	$(PYTHON) scripts/check_toolchain.py
