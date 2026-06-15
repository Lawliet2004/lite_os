# LiteNix User Management (Phase 8.9)

This document describes the multi-user infrastructure delivered in Phase 8.9.
The goal of this phase was to move LiteNix from "everything as UID 0" to a
real, enforced multi-user system suitable for an Alpine/Tiny-Core-class
distribution.

## Overview

LiteNix now ships the following user-management tooling:

| Tool | Path | Purpose |
| --- | --- | --- |
| `login` | `/bin/login` | Interactive login on the system console |
| `passwd` | `/bin/passwd` | Change or lock/unlock account passwords |
| `su` | `/bin/su` | Switch user (root → user, or user → root with auth) |
| `id` | `/bin/id` | Print effective uid/gid/groups |
| `whoami` | `/bin/whoami` (→ `/bin/id`) | Print effective user name |
| `groups` | `/bin/groups` (→ `/bin/id`) | Print group membership |
| `useradd` | `/sbin/useradd` | Create a new account |
| `userdel` | `/sbin/userdel` (→ `/sbin/useradd`) | Remove an account |

All six tools are static ELFs built against `user/libc-lite` and live in the
initramfs. The kernel side already has `getuid`, `geteuid`, `getgid`,
`getegid`, `setresuid`, `setresgid` (syscalls 102/104/107/108/117/119) wired
up with proper privilege enforcement.

## Password storage

Passwords are hashed using a custom but deterministic `$5$` SHA-256 scheme:

```
$5$<salt>$<hex64>
```

where the hex digest is `SHA-256(salt || password)` iterated **5000** rounds,
each round feeding `SHA-256(prev_digest || salt)`. The same scheme is
implemented identically in:

- `user/libc-lite/libc_lite.c` (`pw_hash`, `pw_verify`)
- `scripts/hash_password.py` (used by the build to seed `/etc/shadow`)

This means the in-image `/bin/passwd` and the host build agree on hashes
bit-for-bit, so we can pre-seed account passwords during ISO creation and
still let users change them at runtime.

### Default credentials

The Makefile seeds the root account with the password **`root`**:

```make
@LITENIX_SALT_ROOT=L1teN1xS $(PYTHON) scripts/hash_password.py root=root \
    > $(BUILD_DIR)/initramfs-root/etc/shadow
```

To change the default before building an ISO:

```sh
LITENIX_SALT_ROOT=anything python scripts/hash_password.py root=mynewpass \
    > build/initramfs-root/etc/shadow
make iso
```

## File formats

LiteNix follows the standard Linux `/etc/passwd` and `/etc/shadow` formats:

```
# /etc/passwd
root:x:0:0:root:/home/root:/bin/sh
alice:x:1000:1000:alice:/home/alice:/bin/sh

# /etc/shadow
root:$5$L1teN1xS$518e386479a201f99786ab73992e370d2c182c5de0af223288c5387271a90e14:0:0:99999:7:::
alice:$5$abcDEF12$3a6e...:0:0:99999:7:::
```

Locked accounts use the conventional `!` or `*` prefix in the shadow hash
field, and accounts with an empty hash require no password.

## Atomic shadow updates

`shent_set_hash()` (in libc-lite) writes the new shadow file to
`/etc/shadow.new`, then `rename()`s it over `/etc/shadow`. If the underlying
filesystem does not support rename, it falls back to a direct truncating
write. This means a power loss during `passwd` does not corrupt the shadow
file.

## Login flow

`/bin/login` is launched by PID 1 (init) in normal-boot mode. It:

1. Reads `/etc/issue` and displays it
2. Prompts for username, then password (with terminal ECHO disabled via
   `TCGETS` / `TCSETS` ioctls)
3. Looks up the account in `/etc/passwd`, then the hash in `/etc/shadow`
4. Compares using `pw_verify`
5. On success: `setresgid(gid, gid, gid)`, `setresuid(uid, uid, uid)`,
   `chdir(home)`, and `execve(shell, …)` with `USER=`, `HOME=`, `SHELL=`,
   `PATH=`, `TERM=` set in the environment
6. On failure: prints `Login incorrect`, retries up to 3 times then exits 1

`init.c` loops on login exit so logging out drops you back to the prompt.

The `/bin/login --noauth <user>` form skips authentication and is intended
only for recovery boots / in-kernel tests.

## Privilege enforcement

The kernel enforces credentials in `sys_setresuid` / `sys_setresgid`:

```c
if (p->euid != 0) return -EPERM;
```

Therefore once a process has dropped privileges with `setresuid(uid, uid, uid)`
for `uid != 0`, it **cannot** regain root. This is verified in Test 32.9:

```c
if (setresuid(1000, 1000, 1000) != 0) exit(21);
if (setresuid(0, 0, 0) == 0) exit(24);  /* must fail */
```

## Copy-on-write fix in uaccess

While implementing this phase a latent kernel bug was uncovered:
`vmm_validate_user_ptr_writable` did not understand COW-marked pages. After
any `fork()`, the parent's writable pages have `VMM_WRITABLE` cleared and
`VMM_COW` set. When the kernel later called `copy_to_user` into one of these
pages, the validator returned `EFAULT` even though the PTE would have been
made writable by a CPU-level write fault.

Fix: `vmm_validate_user_ptr_ex` now calls `vmm_handle_cow` when it sees a
COW-marked PTE during a writable check, then re-walks the PTE. This makes
`read()` and other writing syscalls work correctly into a forked process's
data/BSS pages.

See `kernel/arch/x86_64/memory/vmm.c::vmm_validate_user_ptr_ex` for the
implementation.

## Verification

The verifier `make verify-boot` now requires the boot log to contain:

```
USER_MGMT: all tests passed
```

This is printed only after init's `Test 32` exercises:

1. `getuid()` / `getgid()` return 0 for PID 1
2. SHA-256 NIST `abc` vector
3. `pw_hash` / `pw_verify` round-trip
4. `pwent_by_name("root")` returns the seeded root entry
5. `shent_by_name("root")` returns the seeded shadow entry
6. `pw_verify("root", root_hash)` succeeds — confirms Python/C agreement
7. `shent_set_hash` round-trip (set + read back + restore)
8. `/bin/id` exits 0
9. `useradd -p guestpw guest` creates a working account and verifies the
   hashed password is recoverable via `pw_verify`
10. `userdel guest` removes the account
11. Forked child drops to UID 1000 and cannot regain UID 0
12. `/bin/login` is present and exec'able

## Future work

This phase covers the userspace mechanics. Open items for follow-up phases:

- **Phase 9.0 (now done)**: VFS permission enforcement + setuid exec.
- VFS-level `chmod()`/`chown()` ownership-restriction (already done in 9.0).
- Setuid/setgid bit on `execve()` for tools that need elevation (already done in 9.0).
- `/etc/group` supplementary groups (currently only primary group is honored).
- `sudo`-like authorization framework or polkit-style policy.
- PAM / pluggable auth (deferred indefinitely — out of scope for hobby distro).
- User home directory templates from `/etc/skel`.

## Phase 9 addendum — setuid-root binaries

The following binaries are installed setuid root in the initramfs so they can
read/write `/etc/shadow` and call `setresuid(uid, uid, uid)` on behalf of
unprivileged users:

| Path | Mode | Owner | Purpose |
| --- | --- | --- | --- |
| `/bin/login` | 04755 | root | Read `/etc/shadow`, start user shell |
| `/bin/passwd` | 04755 | root | Update `/etc/shadow` |
| `/bin/su` | 04755 | root | Switch to another user with auth |

The mode override happens in `scripts/make_initramfs.py` because the host
filesystem (Windows NTFS) cannot represent setuid bits. The Python script
re-stamps the tar header to `04755` so the kernel's initramfs unpacker
records the right `mode_val` in each VFS node.

`/etc/shadow` is similarly stamped to mode `0600` at tar-pack time, and
`shent_set_hash()` re-stamps the mode after every rewrite so `passwd` cannot
accidentally make the shadow file world-readable.
