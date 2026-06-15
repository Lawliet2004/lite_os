#!/usr/bin/env bash
# scripts/build-kernel-dev.sh
# Minimal kernel-only build that avoids the broken user-space build chain
# (dhcpcd, hostname, show_creds, dynamic-musl zig requirements, etc.).
# Produces build/litenix.elf and a tiny initramfs for SMP bring-up.

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

CC="${CC:-clang --target=x86_64-freestanding-none -mno-red-zone}"
LD="${LD:-clang --target=x86_64-freestanding-none}"
AS="${AS:-nasm}"
OBJCOPY="${OBJCOPY:-llvm-objcopy}"
CFLAGS_KERNEL=(
  -std=c11 -ffreestanding -fno-builtin -fno-stack-protector
  -fno-pic -fno-pie -m64 -mcmodel=kernel -mno-red-zone
  -fno-omit-frame-pointer -mno-mmx -mno-sse -mno-sse2 -msoft-float
  -Wall -Wextra -Werror -O2 -g
  -Ikernel/include
)
CFLAGS_USER=(
  -std=c11 -ffreestanding -fno-builtin -fno-stack-protector
  -fno-pic -fno-pie -m64 -fno-omit-frame-pointer
  -mno-mmx -mno-sse -mno-sse2
  -Wall -Wextra -Werror -O2 -g
  -Iuser/libc-lite
)
ASFLAGS=(-f elf64)
LDFLAGS=(-T kernel/arch/x86_64/linker.ld -nostdlib -static -z max-page-size=0x1000)

BUILD="$ROOT/build"
mkdir -p "$BUILD/kernel" "$BUILD/user"

C_SOURCES=(
  kernel/arch/x86_64/cpu/gdt.c
  kernel/arch/x86_64/smp.c
  kernel/arch/x86_64/interrupt/idt.c
  kernel/arch/x86_64/interrupt/interrupt.c
  kernel/arch/x86_64/interrupt/pic.c
  kernel/arch/x86_64/memory/vmm.c
  kernel/drivers/pit.c
  kernel/mm/heap.c
  kernel/mm/pmm.c
  kernel/core/init.c
  kernel/core/kernel.c
  kernel/core/panic.c
  kernel/core/printk.c
  kernel/drivers/serial.c
  kernel/drivers/ata_pio.c
  kernel/drivers/tty.c
  kernel/drivers/vga_text.c
  kernel/lib/string.c
  kernel/sched/scheduler.c
  kernel/sched/task.c
  kernel/sched/wait_queue.c
  kernel/kernel/spinlock.c
  kernel/arch/x86_64/syscall/syscall_entry.c
  kernel/sys/syscall_table.c
  kernel/sys/sys_exit.c
  kernel/sys/sys_file.c
  kernel/sys/sys_process.c
  kernel/sys/sys_mem.c
  kernel/sys/sys_epoll.c
  kernel/mm/uaccess.c
  kernel/core/elf_loader.c
  kernel/fs/vfs.c
  kernel/fs/initramfs.c
  kernel/fs/ext2.c
  kernel/fs/block.c
  kernel/drivers/pci.c
  kernel/drivers/virtio_net.c
  kernel/net/net.c
  kernel/net/eth.c
  kernel/net/arp.c
  kernel/net/ipv4.c
  kernel/net/icmp.c
  kernel/net/udp.c
  kernel/net/tcp.c
  kernel/net/socket.c
)

ASM_SOURCES=(
  kernel/arch/x86_64/entry.S
  kernel/arch/x86_64/cpu/gdt_load.S
  kernel/arch/x86_64/interrupt/isr.S
  kernel/arch/x86_64/memory/switch.S
  kernel/arch/x86_64/syscall/syscall_stub.S
  kernel/arch/x86_64/ap_trampoline.S
  kernel/lib/setjmp.S
  kernel/core/user_binaries.S
  kernel/fs/initramfs_binary.S
)

# -----------------------------------------------------------------
# Step 1: user binaries (init.elf, test_read_kernel.elf, test_privileged.elf)
# -----------------------------------------------------------------
$AS "${ASFLAGS[@]}" user/tests/test_read_kernel.S -o "$BUILD/user/test_read_kernel.o"
$LD -T user/user.ld -nostdlib -static "$BUILD/user/test_read_kernel.o" -o "$BUILD/user/test_read_kernel.elf"
$AS "${ASFLAGS[@]}" user/tests/test_privileged.S -o "$BUILD/user/test_privileged.o"
$LD -T user/user.ld -nostdlib -static "$BUILD/user/test_privileged.o" -o "$BUILD/user/test_privileged.elf"
# init.elf: a tiny assembly init that prints a banner and exits
# cleanly. The real user/init/init.c is broken under strict clang, so
# we use the .S version in user/init/init.S instead. The kernel link
# step incbin's this into user_binaries.o.
$AS "${ASFLAGS[@]}" user/init/init.S -o "$BUILD/user/init_stub.o"
$LD -T user/user.ld -nostdlib -static "$BUILD/user/init_stub.o" -o "$BUILD/user/init.elf"

# -----------------------------------------------------------------
# Step 2: minimal initramfs.tar (must exist before initramfs_binary.S is assembled)
# -----------------------------------------------------------------
INITRAMFS_ROOT="$BUILD/initramfs-root"
rm -rf "$INITRAMFS_ROOT"
mkdir -p "$INITRAMFS_ROOT/bin" "$INITRAMFS_ROOT/sbin" \
         "$INITRAMFS_ROOT/etc" "$INITRAMFS_ROOT/dev" \
         "$INITRAMFS_ROOT/proc" "$INITRAMFS_ROOT/tmp" \
         "$INITRAMFS_ROOT/lib" "$INITRAMFS_ROOT/usr/bin" \
         "$INITRAMFS_ROOT/usr/sbin" "$INITRAMFS_ROOT/usr/lib" \
         "$INITRAMFS_ROOT/usr/share" "$INITRAMFS_ROOT/tests" \
         "$INITRAMFS_ROOT/home" "$INITRAMFS_ROOT/run" \
         "$INITRAMFS_ROOT/var" "$INITRAMFS_ROOT/var/log" \
         "$INITRAMFS_ROOT/sys" "$INITRAMFS_ROOT/lib64"
cp "$BUILD/user/init.elf" "$INITRAMFS_ROOT/sbin/init"

# Phase 1 dev: just enough for the init's first test (open /hello.txt).
cat > "$INITRAMFS_ROOT/etc/motd" <<'EOF'
LiteNix dev initramfs (minimal SMP build)
EOF
cat > "$INITRAMFS_ROOT/etc/hostname" <<'EOF'
litenix
EOF
cat > "$INITRAMFS_ROOT/etc/hosts" <<'EOF'
127.0.0.1 localhost
EOF
cat > "$INITRAMFS_ROOT/etc/profile" <<'EOF'
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
EOF
cat > "$INITRAMFS_ROOT/etc/passwd" <<'EOF'
root:x:0:0:root:/home/root:/bin/sh
EOF
cat > "$INITRAMFS_ROOT/etc/group" <<'EOF'
root:x:0:
EOF
cat > "$INITRAMFS_ROOT/etc/os-release" <<'EOF'
NAME="LiteNix"
VERSION="0.1"
EOF
cat > "$INITRAMFS_ROOT/etc/issue" <<'EOF'
LiteNix 0.1
EOF
echo "Hello from LiteNix initramfs!" > "$INITRAMFS_ROOT/hello.txt"
# Busybox symlink for applets (if sh applets are called)
ln -sf /sbin/init "$INITRAMFS_ROOT/bin/sh" 2>/dev/null || true

python3 scripts/make_initramfs.py "$INITRAMFS_ROOT" "$BUILD/initramfs.tar"

# -----------------------------------------------------------------
# Step 3: kernel C objects
# -----------------------------------------------------------------
for src in "${C_SOURCES[@]}"; do
  obj="$BUILD/${src%.c}.o"
  mkdir -p "$(dirname "$obj")"
  if [ "$src" -nt "$obj" ]; then
    echo "CC  $src"
    $CC "${CFLAGS_KERNEL[@]}" -c "$src" -o "$obj"
  fi
done

# -----------------------------------------------------------------
# Step 4: kernel ASM objects (last, so initramfs_binary.S finds initramfs.tar)
# -----------------------------------------------------------------
for src in "${ASM_SOURCES[@]}"; do
  obj="$BUILD/${src%.S}.o"
  mkdir -p "$(dirname "$obj")"
  if [ "$src" -nt "$obj" ] || [ ! -f "$obj" ]; then
    echo "AS  $src"
    $AS "${ASFLAGS[@]}" "$src" -o "$obj"
  fi
done

# -----------------------------------------------------------------
# Step 5: link kernel
# -----------------------------------------------------------------
OBJECTS=()
for src in "${C_SOURCES[@]}"; do
  OBJECTS+=("$BUILD/${src%.c}.o")
done
for src in "${ASM_SOURCES[@]}"; do
  OBJECTS+=("$BUILD/${src%.S}.o")
done

$LD "${LDFLAGS[@]}" "${OBJECTS[@]}" -o "$BUILD/litenix.elf"
echo
echo "Built $BUILD/litenix.elf"
ls -la "$BUILD/litenix.elf"
