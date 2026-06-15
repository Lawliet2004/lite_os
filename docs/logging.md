# LiteNix System Logging (Phase 9.2)

This document describes LiteNix's user-space logging infrastructure built on
top of the kernel's 16 KiB `/dev/kmsg` ring buffer.

## Components

| Component | Path | Purpose |
| --- | --- | --- |
| `klogd` | `/sbin/klogd` | Drains the kernel ring buffer to a file in userspace |
| `logger` | `/bin/logger` | Writes a user message into the kernel ring buffer |
| `/var/log/kern.log` | — | Persistent log file |

`/dev/kmsg` already exists in the kernel — it records every `printk()`
output into a 16 KiB ring buffer. Without a userspace daemon, the oldest
entries get overwritten once the buffer fills. `klogd` is the
standard solution: it tails the ring and appends each chunk to
`/var/log/kern.log` with a timestamp.

## `klogd`

```c
/* Lifecycle
 *   klogd                 -> run in foreground (used by init for testing)
 *   klogd --daemon        -> fork into the background (rcS.sh)
 *   klogd --once          -> drain whatever is currently in kmsg and exit
 */
```

- Opens `/dev/kmsg` for read, `/var/log/kern.log` for append.
- On each tick (1 s when daemonizing, immediate in foreground), it
  lseeks to its tracked position, reads up to 4 KiB from kmsg, and
  writes `[YYYY-MM-DD HH:MM:SS.mmm] ` + bytes to the log.
- Tracks its position in the kmsg ring with `lseek()` and an
  `off_t`; multiple `klogd` invocations don't double-write because
  they always seek to the end of the file before reading.
- The `--daemon` form writes `/run/klogd.pid` and exits the parent.

In `rcS.sh` the daemon is started early so the messages from the rest
of the boot (services, DHCP handshake, etc.) are persisted:

```sh
/sbin/klogd --daemon
echo "klogd started"
```

## `logger`

```c
/* Usage
 *   logger <message>...            -> write "<args>\n" to /dev/kmsg
 *   logger -p <prio> <message>...  -> accept a syslog-style priority
 *                                    (LiteNix ignores the value but
 *                                    accepts the flag for compat.)
 */
```

Concatenates args with single spaces, appends a newline, and writes
to `/dev/kmsg` via the kernel's `kmsg_write` hook. The message
appears in:
- the kernel's ring buffer (immediately)
- `/var/log/kern.log` (after klogd picks it up on its next tick)

## `rcS.sh` integration

`/etc/init.d/rcS` now does:

```sh
/sbin/klogd --daemon
echo "klogd started"

# ... network config, service startup ...
/bin/logger "LiteNix boot complete"
```

The boot marker appears in `kern.log` so a `tail -f` of the log
after boot shows when the system finished initializing.

## Init Test 35 (SYSLOG)

`init.c`'s Test 35 verifies the round trip end-to-end:

1. `/sbin/klogd` and `/bin/logger` are present.
2. `/var/log/kern.log` is writable.
3. `logger "SYSLOG-TEST-MARKER-12345"` writes to kmsg.
4. Reading `/dev/kmsg` directly returns the marker.
5. `klogd --once` drains the buffer to the log file.
6. The log file contains the marker AND starts with `[YYYY-MM-DD ...]`
   (timestamp prepended by klogd).
7. A second `logger` + `klogd --once` round appends the new marker
   without overwriting the first.

The verify-boot script greps for `SYSLOG: all tests passed`.

## Wire format

`/var/log/kern.log` lines look like:

```
[2026-06-14 12:34:56.789] LiteNix version 1.0.0 (x86_64) Freestanding C
[2026-06-14 12:34:56.789] VFS: mounted /hello.txt (type=FILE, size=30 ...)
[2026-06-14 12:35:01.012] SYSLOG-TEST-MARKER-12345
[2026-06-14 12:35:01.013] SYSLOG-SECOND-MARKER-67890
```

The timestamp is UTC. There is no log rotation daemon — `logrotate`
is not yet in the userspace. Operators can `> kern.log` to clear the
log; `klogd`'s lseek tracking will resume from the new file end.

## Known limitations

- No `kmsg` rewind support: if you want to re-read old kernel
  messages that have already been written to disk, you read the
  log file. The `/dev/kmsg` ring is forward-only.
- No rate limiting. A printk storm will fill the 16 KiB ring and
  drop the oldest entries (and `klogd` will only see the most
  recent ones that haven't been overwritten yet).
- `klogd` uses `gettimeofday` for timestamps. Without a wall-clock
  set, the timestamps will be since the Unix epoch.
- No structured logging or severity levels. Every line is
  informational; "warning"/"error" routing is future work.
- The boot-completion marker `logger "LiteNix boot complete"` is
  best-effort; if the kernel ring overflows before klogd reads
  it, the marker will be lost.

## Build

Both binaries are built and installed by the regular Makefile
chain. `MODE_OVERRIDES` in `scripts/make_initramfs.py` already
covers `/etc/shadow` mode 0600 and the setuid bits; no further
overrides are needed for logging because `/var/log/kern.log` is
created on first use with mode 0644 owned by root.

## Future work

- A real `syslogd` listening on `/dev/log` (AF_UNIX SOCK_DGRAM) so
  userspace processes don't have to share the kernel ring buffer.
- `logrotate` (or equivalent) so `/var/log/kern.log` doesn't grow
  without bound.
- Severity levels (info/warn/err/crit) on `logger -p` so the file
  can be filtered.
- A real-time log-tail command (`tail -f /var/log/kern.log`).
