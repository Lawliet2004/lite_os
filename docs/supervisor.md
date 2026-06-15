# Service Supervision (Phase 9.3)

Phase 9.3 replaces the shell-based `service` wrapper that came out of
Phase 6 with a proper C-based service controller and a small respawn
supervisor. The design is intentionally simple and follows the
**OpenRC / runit / s6** convention rather than systemd-style units:

* every service is described by a single `KEY="VALUE"` `.conf` file
  under `/etc/services.available/`
* the boot order is determined by `AFTER="..."` declarations inside
  each `.conf`
* the kernel of control is `/sbin/svc` (a 4 KiB-ish userspace
  binary)
* a separate `/sbin/supervisor` daemon watches enabled+respawn
  services and re-runs them if they die

The shell wrapper is still callable for the few scripts that haven't
been ported, but `rcS.sh` and the verification tests use the new
binary exclusively.

## Filesystem layout

```
/etc/services.available/<name>.conf    service definition
/etc/services.enabled                  one <name> per line
/run/services/<name>.pid               running pid, or stale on crash
/run/supervisor.pid                    supervisor daemon pid
/var/log/services/<name>.log           per-service stdout+stderr
```

The `<name>.conf` files are owned by root and live in the initramfs
(so the boot is reproducible). They are also re-installed into the
persistent `/etc/` after a successful boot.

## Service definition format

```ini
NAME="http_server"
DESCRIPTION="Basic HTTP server on port 80"
EXEC="/usr/sbin/http_server"
ARGV=""                              # optional; default = none
AFTER="udp_echo"                    # space-separated dependency list
RESPAWN="yes"                       # yes/no (default: no)
MAX_RESTARTS="5"                    # default: 5 within RESPAWN_GRACE_SECS
LOG_FILE="/var/log/services/http_server.log"
USER=""                             # placeholder for future privilege drop
```

The parser is in `user/svc.c` and is strict about the
`KEY="VALUE"` shape; lines starting with `#` and blank lines are
ignored. Unknown keys are ignored too, so `.conf` files can carry
forward-compatible metadata.

## `svc` (the controller)

`/sbin/svc` understands:

| Command                | Description                                    |
|------------------------|------------------------------------------------|
| `svc list`             | enumerate all `.conf` files with their state    |
| `svc <name> start`     | fork+exec the service, write its PID file       |
| `svc <name> stop`      | SIGTERM, then SIGKILL after a 3 s grace        |
| `svc <name> status`    | print `running`/`stopped`/`crashed` and pid    |
| `svc <name> restart`   | stop then start                                |
| `svc <name> enable`    | add the name to `/etc/services.enabled`        |
| `svc <name> disable`   | remove it                                      |
| `svc start-enabled`    | start every service in the enabled file in dep order |

Return codes follow the systemd-ish convention (0=running,
1=crashed, 3=stopped) so scripts can chain on the result.

`start-enabled` implements single-level dependency ordering: it
re-scans the enabled list until a full pass makes no progress. A
service whose `AFTER="..."` dependencies are not yet `running` is
deferred. A budget of 32 passes prevents infinite loops from
broken `.conf` files.

`start` does the work in the canonical way:

1. `mkdir -p /run/services` and `/var/log/services` (so first-time
   boots don't race with `rcS.sh`).
2. Open the log file in `O_WRONLY | O_CREAT | O_APPEND` and
   `dup2()` it onto the child's fd 1 and fd 2.
3. `execve()` the service. The `ARGV` field, if present, is split
   on whitespace and passed as a proper `argv[]`. The `PATH` env
   is hard-coded so a missing shell doesn't break service startup.
4. Write `<pid>\n` to `/run/services/<name>.pid`.
5. `wait4(pid, &status, WNOHANG, 0)` so we don't leak a zombie for
   services that exit faster than the caller checks.

`stop` reads the PID file, sends `SIGTERM`, polls every 100 ms for
3 seconds, then sends `SIGKILL` if it's still alive. After
reaping, the PID file is removed. A stale PID file (pid no longer
exists) is cleaned up silently.

## `supervisor` (the respawn daemon)

`/sbin/supervisor --daemon` forks into the background, then polls
every second for any enabled service whose `.conf` declares
`RESPAWN="yes"`. When a service's PID file is missing or the
recorded pid is dead, the supervisor re-runs `svc <name> start` for
it, subject to a `MAX_RESTARTS` budget within
`RESPAWN_GRACE_SECS` (30 s by default). If the service stays alive
longer than the grace period, the restart counter is reset —
matching the OpenRC/runit convention.

The supervisor writes its own pid to `/run/supervisor.pid` so an
`init` script can manage it. It is expected to run for the entire
uptime of the system.

## Integration with `rcS.sh`

The boot script now does:

```sh
/sbin/klogd --daemon
/sbin/dhcpcd 2>/dev/null || /sbin/ifconfig eth0 10.0.2.15 ...
/sbin/svc start-enabled
/sbin/supervisor --daemon
```

`start-enabled` respects the `AFTER="..."` graph, so `http_server`
(which depends on `udp_echo`) does not start before its sibling.
`klogd` is started before the services so their early stdout
already lands in `/var/log/kern.log`.

## Test 36 (SUPERVISOR)

The init process in test mode runs `Test 36` after `Test 35`
(SYSLOG) and verifies:

* `/sbin/svc` and `/sbin/supervisor` are present
* `/etc/services.available/udp_echo.conf` and `http_server.conf`
  exist
* `svc list` enumerates both definitions
* `svc hostname start` creates a pid file
* `svc hostname status` returns a valid state (0/1/3)
* `svc http_server disable` removes the name from
  `/etc/services.enabled`, then `svc http_server enable` puts it
  back
* `svc start-enabled` starts everything in dependency order
* `supervisor --daemon` writes its pid file and can be killed
  cleanly

A failure in any sub-check prints `Init ERROR: ...` and the test
panics the boot. On success, init prints
`SUPERVISOR: all tests passed`, and the `verify-boot` Makefile
target greps the serial log for that marker.

## Known limitations / future work

* No `TYPE=oneshot` or "service runs at boot but doesn't restart"
  mode; the supervisor only respawns things flagged `RESPAWN="yes"`.
* No privilege drop — the `USER=` field is a placeholder. The
  service runs as root, which is fine for the in-tree UDP/HTTP
  daemons but not safe in general.
* No cgroups, no resource limits. The plan is to add cgroups when
  the kernel's `cgroup-v2` driver lands.
* No `svc reload` (SIGHUP-aware). The current `restart` does the
  job but the service has to be re-exec'd.
* The supervisor polls every second. With `inotify` on the PID
  files this could become event-driven, but the polling overhead
  is negligible at this scale.
* Dependency cycles would cause `start-enabled` to exit with

## `svc log <name>` (Phase 9.4)

`svc <name> log` reads the per-service log file and writes it to
stdout. The log path is resolved in the same order as `start`:

1. The `LOG_FILE="..."` field in `/etc/services.available/<name>.conf`,
   if set.
2. Otherwise `/var/log/services/<name>.log` (the default created
   by `svc start`).

The default cap is 64 KiB per dump — enough for the in-tree
daemons and small enough to keep terminal output bounded. If the
log is larger the read stops at 64 KiB and prints
`log truncated at N bytes (use \`cat\` for full dump)` to stderr-style
stdout. The file descriptor is drained before close so a future
caller can read again without re-opening.

Return codes follow the `status` convention so scripts can branch
on them:

| Code | Meaning                                        |
|------|------------------------------------------------|
| 0    | Log printed in full.                           |
| 1    | Definition or log file exists but open failed. |
| 2    | `read()` error mid-stream.                      |
| 3    | Log doesn't exist yet (the service was never started; message: `svc: <name> has no log yet (try \`svc <name> start\` first)`). |

The "no log yet" case is treated separately from a real error
because a fresh install has no service logs and the user just
needs to start the service first.

## `svc describe <name>` (Phase 9.5)

`svc <name> describe` prints the parsed `.conf` fields together
with the live runtime state in a greppable `KEY=VALUE` format. The
output has two sections:

* **Definition** — `file`, `name`, `description`, `exec`, `argv`,
  `after`, `respawn`, `max_restarts`, `log_file`. Every key is
  always printed (the empty ones read as `(none)` or `(default)`)
  so a downstream tool can parse the output without remembering
  which fields are optional.
* **Runtime state** — `enabled` (from `/etc/services.enabled`),
  `state` (`running`/`crashed`/`stopped`), `pid_file` (path +
  contents), `log_file_path` (the actual on-disk path that
  `svc log` and the supervisor would read).

This is a "doctor" command: it's what you run when a service
isn't starting and you want to see both the operator-intended
config and the on-disk reality in one screen. Unlike
`svc <name> start`, `describe` never starts the service and
never changes anything on disk, so it's safe to call from
recovery contexts.

## `svc tree` (Phase 9.6)

`svc tree` walks `/etc/services.available/` and prints the
`AFTER="..."` dependency graph as an indented tree, with each
node annotated by its live `state` (`running`/`stopped`/
`crashed`) and its `enabled` flag. Roots — services that
nothing else depends on — come first; deeper levels are services
that have to come up after the root.

```
svc: dependency tree (root services at top):
  udp_echo [stopped]
  http_server [stopped]  (enabled at boot)
    udp_echo
  hostname [stopped]
```

Three things to notice:

* `http_server` lists `udp_echo` as an `AFTER=` dep, so it
  shows up indented under the root. The fact that `udp_echo`
  also appears at the root is fine — the tree just walks
  every reachable node once; cycle detection only fires for
  actual cycles.
* `(enabled at boot)` is the same `state=enabled` flag
  `start-enabled` reads from `/etc/services.enabled`.
* `(no .conf)` is printed for a node that's referenced as an
  `AFTER=` dep but doesn't have its own `.conf` file — useful
  for catching typos in the dependency graph before they cause
  silent boot-order issues.

Cycles and unreachable nodes are collected at the bottom
under a `[unreachable / cyclic]` header, and individual cycles
are flagged with a `[cycle]` tag on the offending node so the
loop isn't recursed into forever. The depth is capped at 8
levels to defend against pathological inputs.

This is the "is anything actually broken about my dependency
graph?" command — the same way `start-enabled` is the
"boot it now" command.

## `svc check [name]` (Phase 9.7)

`svc check` validates the on-disk service state without
starting anything. With no name, it checks every `.conf` in
`/etc/services.available/`; with a name, it checks just that
service. It runs four probes per service:

* **`.conf` parse** — `load_def` is called; a parse failure
  flags the service.
* **`EXEC=` reachability** — `stat()` the path; missing
  executables (typo, deleted binary, wrong mount) are caught
  before the supervisor tries to start the service.
* **`AFTER=` resolution** — every dependency name in
  `AFTER="..."` is checked against the directory's
  `getdents` output; an undefined dep is a typo that would
  otherwise silently get ignored by `start-enabled`.
* **orphan enables** — cross-check `/etc/services.enabled`
  for entries that have no matching `.conf`; these are usually
  the result of a half-finished `svc enable` or a stale
  enabled-list across a `.conf` rename.

Output is one line per service:

```
svc: udp_echo                 OK
svc: http_server              OK
svc: hostname                 OK
svc: check OK (3 services)
```

A service with a problem gets a `FAIL` line plus a bracketed
list of the specific issues:

```
svc: http_server              FAIL [exec missing: /usr/sbin/http_server]
svc: check found 1 issue
```

Exit codes follow the standard convention so a CI / pre-deploy
gate can branch on them:

| Code | Meaning                                  |
|------|------------------------------------------|
| 0    | All services passed every probe.         |
| 1    | One or more issues were found.            |
| 2    | `svc check` itself errored (e.g. can't read `/etc/services.available/`). |

`svc check` is read-only: it never starts a service, never
modifies a `.conf`, never edits `/etc/services.enabled`. The
only side effect is a few bytes of `printf` to stdout. That
makes it safe to call from boot-time recovery, from a
pre-deploy CI step, or from a periodic cron that watches for
"drift" between the enabled list and the on-disk definitions.
  `deferred != 0`. The Makefile check still passes; future work
  would add a cycle detector and a clearer error.

## Files added in Phase 9.3

* `user/svc.c` (~600 lines)
* `user/supervisor.c` (~250 lines)
* `user/udp_echo.conf`
* `user/http_server.conf`
* `user/hostname.conf`
* `docs/supervisor.md` (this file)

## Files modified in Phase 9.3

* `user/rcS.sh` — uses the new `svc start-enabled` instead of the
  shell loop
* `user/init/init.c` — adds `Test 36: SUPERVISOR`
* `Makefile` — adds the build rules, the initramfs install of the
  two new binaries and three new `.conf` files, and the
  `SUPERVISOR: all tests passed` grep in `verify-boot`
* `docs/linux-os-plan.md` — marks 9.3 as completed
* `README.md` — adds the new `svc`/`supervisor` quick commands
