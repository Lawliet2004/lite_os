# Linux Compatibility Report

Last updated: 2026-06-11

This pass removes several high-visibility ABI gaps where LiteNix exposed Linux syscall numbers but still returned `-ENOSYS`. `mremap`, `madvise`, `sendmsg`, `recvmsg`, `socketpair`, `setsockopt`, `getsockopt`, and `shutdown` now have kernel implementations, and the event-waiting path now has a shared I/O notification queue for `select`, `pselect6`, and `epoll_wait`.

Programs that should work now that previously tended to fail earlier in startup or probe logic:

- BusyBox- and musl-linked tools that use `socketpair(AF_UNIX, SOCK_STREAM)` for internal process plumbing
- userland that probes `SO_REUSEADDR`, `SO_TYPE`, `SO_ERROR`, or `SO_ACCEPTCONN`
- simple stream-socket code paths using `sendmsg` / `recvmsg` instead of `sendto` / `recvfrom`
- runtimes and allocators that depend on `mremap` or `madvise(MADV_DONTNEED)`
- CLI tools whose behavior depends on mode-bit access checks instead of an effectively root-only VFS

Still incomplete for broader Linux compatibility:

- boot verification for the new event-waiting slice is still pending
- full `MAP_SHARED`
- ancillary data in `sendmsg` / `recvmsg`
- Unix-domain sockets beyond `AF_UNIX` stream `socketpair`
- setuid/setgid executable semantics
- full tty job control
