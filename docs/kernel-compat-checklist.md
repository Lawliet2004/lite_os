# Kernel Compatibility Checklist

Last updated: 2026-06-11

## Linux syscall compatibility

- `select`: code updated to block on shared I/O notifications; boot verification pending
- `pselect6`: code updated to block on shared I/O notifications and respect a temporary signal mask; boot verification pending
- `epoll_create1`: implemented
- `epoll_ctl`: implemented
- `epoll_wait`: code updated to block on shared I/O notifications; boot verification pending
- `sendmsg`: implemented for socket-backed descriptors without ancillary data
- `recvmsg`: implemented for socket-backed descriptors without ancillary data
- `socketpair`: implemented for `AF_UNIX` `SOCK_STREAM`
- `setsockopt`: implemented for basic `SOL_SOCKET` options
- `getsockopt`: implemented for basic `SOL_SOCKET` options
- `shutdown`: implemented
- `mremap`: implemented for shrink, in-place grow, and `MREMAP_MAYMOVE`
- `madvise`: implemented for common hints plus `MADV_DONTNEED`
- `prctl`: implemented for thread naming and dumpable queries
- `getresuid` / `setresuid`: implemented
- `getresgid` / `setresgid`: implemented

## Process and thread compatibility

- `clone`: fork-style and thread-style clone supported
- `futex`: `FUTEX_WAIT` and `FUTEX_WAKE` supported
- process groups / sessions: basic support present
- `kill(pid < 0, sig)`: process-group delivery supported
- real/effective/saved UID and GID: implemented

## Memory management

- anonymous `mmap`: implemented
- file-backed `mmap`: implemented for `MAP_PRIVATE`
- `MAP_SHARED`: not implemented
- file-backed private COW fault path: implemented
- FD limit: `256`
- VMA limit: `128`

## Filesystem and mount model

- persistent ext2-backed `/persist`: implemented
- mount table infrastructure: implemented
- `/proc/mounts`: implemented from the VFS mount table, with a synthetic rootfs fallback
- boot image mounts/layout for `/proc`, `/sys`, `/dev`, `/tmp`, `/run`: present
- VFS permission checks: enforced on open/access/create/unlink/rename/chmod/chown paths
- ext2 indirect blocks / large-file work: still incomplete

## Security model

- kernel UID/GID state: implemented
- mode-bit permission enforcement: implemented
- `chmod` / `chown` / `umask`: implemented with owner/root restrictions
- setuid/setgid executable semantics: not implemented
- login/session isolation groundwork: partial
