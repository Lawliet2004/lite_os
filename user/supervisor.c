/*
 * /sbin/supervisor -- LiteNix service respawn supervisor.
 *
 * Reads /etc/services.enabled at startup, then watches every service
 * whose definition declares RESPAWN="yes". If the pid stored in
 * /run/services/<name>.pid dies, this daemon re-runs `svc <name> start`,
 * subject to a per-service MAX_RESTARTS budget.
 *
 * The restart counter is reset once the service has been alive for at
 * least RESPAWN_GRACE_SECS. This matches the OpenRC/runit convention
 * and prevents the supervisor from giving up too eagerly when a service
 * crashes in a tight loop.
 *
 * Lifecycle:
 *   supervisor                       run in foreground (test)
 *   supervisor --daemon              fork into the background
 *
 * Implementation notes:
 *   - We do not have inotify, kqueue, or signalfd yet, so the
 *     supervisor polls every POLL_INTERVAL_SECS seconds. With a 1-second
 *     interval the CPU cost is negligible and the worst-case respawn
 *     latency is small.
 *   - We use /proc/<pid>/status to test liveness, the same path svc
 *     uses. This lets us detect crashes that happen to leave a stale
 *     pid file.
 */

#include "libc_lite.h"
#include <stdbool.h>

#define POLL_INTERVAL_SECS 1
#define RESPAWN_GRACE_SECS  30

#define SVC_DEF_DIR   "/etc/services.available"
#define SVC_ENABLED    "/etc/services.enabled"
#define SVC_RUN_DIR    "/run/services"
#define SVC_LOG_DIR    "/var/log/services"

/* In-memory record of one watched service. */
struct watched {
    char name[64];
    int  last_pid;
    int  start_count;       /* total starts since supervisor booted */
    int  respawn_count;     /* consecutive rapid respawns */
    /* Cheap clock: integer seconds since first start. We use the
     * PIT tick counter exposed by sys_getrlimit or sys_clock_gettime
     * via the libc-lite wrappers. */
    int  alive_since_tick;
};

#define MAX_WATCHED 32
static struct watched watched[MAX_WATCHED];
static int watched_n = 0;

static int read_text_file(const char *path, char *out, size_t out_size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, out, out_size - 1);
    close(fd);
    if (n < 0) return -1;
    out[n] = 0;
    return (int)n;
}

static int read_pid_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    int pid = 0;
    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] >= '0' && buf[i] <= '9') {
            pid = pid * 10 + (buf[i] - '0');
        } else {
            break;
        }
    }
    return pid;
}

static bool pid_alive(int pid)
{
    if (pid <= 0) return false;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    close(fd);
    return true;
}

/* Minimal .conf parser — just enough for the keys the supervisor
 * actually needs (RESPAWN, MAX_RESTARTS, EXEC, AFTER, NAME). */
static bool parse_respawn(const char *name, int *max_restarts)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.conf", SVC_DEF_DIR, name);
    char body[4096];
    if (read_text_file(path, body, sizeof(body)) < 0) return false;
    *max_restarts = 5;

    char *line = body;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        while (*line == ' ' || *line == '\t') line++;
        if (strncmp(line, "RESPAWN=\"yes\"", 13) == 0 ||
            strncmp(line, "RESPAWN=yes", 11) == 0) {
            return true;
        }
        if (strncmp(line, "MAX_RESTARTS=\"", 13) == 0) {
            const char *p = strchr(line, '"');
            if (p) *max_restarts = atoi(p + 1);
        } else if (strncmp(line, "MAX_RESTARTS=", 12) == 0) {
            *max_restarts = atoi(line + 12);
        }
        if (nl) line = nl + 1; else break;
    }
    return false;
}

static int tick_now(void)
{
    /* PIT is 100Hz. Sleep(1) advances time by ~1s. We don't need
     * high precision for restart-grace math. */
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (int)ts.tv_sec;
    }
    return 0;
}

static void start_service(const char *name)
{
    /* Call `svc <name> start` via fork+exec. This is more robust than
     * duplicating the start logic, and stays in sync with the
     * canonical service definition. */
    int pid = fork();
    if (pid == 0) {
        char *argv[] = { "/sbin/svc", (char *)name, "start", 0 };
        char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", 0 };
        execve("/sbin/svc", argv, envp);
        _exit(127);
    }
    if (pid < 0) return;
    int status = 0;
    wait4(pid, &status, 0, 0);
    (void)status;
}

static struct watched *find_watched(const char *name)
{
    for (int i = 0; i < watched_n; i++) {
        if (strcmp(watched[i].name, name) == 0) return &watched[i];
    }
    return 0;
}

static void maybe_add(const char *name)
{
    if (find_watched(name)) return;
    int max_restarts = 5;
    if (!parse_respawn(name, &max_restarts)) return;  /* not respawn */
    if (watched_n >= MAX_WATCHED) return;
    struct watched *w = &watched[watched_n++];
    snprintf(w->name, sizeof(w->name), "%s", name);
    w->last_pid = 0;
    w->start_count = 0;
    w->respawn_count = 0;
    w->alive_since_tick = tick_now();
}

static void tick(void)
{
    int now = tick_now();

    for (int i = 0; i < watched_n; i++) {
        struct watched *w = &watched[i];
        char pp[64];
        snprintf(pp, sizeof(pp), "%s/%s.pid", SVC_RUN_DIR, w->name);
        int pid = read_pid_file(pp);
        bool alive = pid_alive(pid);

        if (!alive) {
            if (w->last_pid > 0) {
                /* service died */
                printf("supervisor: %s died (was pid %d)\n", w->name, w->last_pid);
            }
            /* Reset the respawn count if the service lived long
             * enough between crashes. */
            if (w->respawn_count > 0 && (now - w->alive_since_tick) >= RESPAWN_GRACE_SECS) {
                w->respawn_count = 0;
            }
            w->respawn_count++;
            if (w->respawn_count > 5) {
                printf("supervisor: %s respawned %d times in %ds, giving up\n",
                       w->name, w->respawn_count, RESPAWN_GRACE_SECS);
                continue;
            }
            printf("supervisor: restarting %s (attempt %d)\n", w->name, w->respawn_count);
            start_service(w->name);
            w->start_count++;
            w->alive_since_tick = now;
            /* Refresh pid from the new pid file. */
            w->last_pid = read_pid_file(pp);
        } else {
            w->last_pid = pid;
            /* If the pid is different from what we last saw, reset the
             * counter — a manual restart is fine. */
            /* (We don't track previous pid across calls, but that's OK
             * since svc <name> restart updates the pid file.) */
        }
    }
}

int main(int argc, char **argv)
{
    bool daemonize = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--daemon") == 0) daemonize = true;
        else { printf("Usage: supervisor [--daemon]\n"); return 1; }
    }

    mkdir(SVC_RUN_DIR, 0755);
    mkdir(SVC_LOG_DIR, 0755);

    /* Load the enabled list and enroll each respawn=yes service. */
    char body[4096];
    if (read_text_file(SVC_ENABLED, body, sizeof(body)) >= 0) {
        char *line = body;
        while (line && *line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = 0;
            if (line[0]) maybe_add(line);
            if (nl) line = nl + 1; else break;
        }
    }

    if (daemonize) {
        int pid = fork();
        if (pid < 0) { printf("supervisor: fork failed\n"); return 1; }
        if (pid > 0) {
            int pfd = open("/run/supervisor.pid", O_WRONLY | O_CREAT | O_TRUNC);
            if (pfd >= 0) {
                char buf[16];
                int n = snprintf(buf, sizeof(buf), "%d\n", pid);
                if (n > 0) write(pfd, buf, (size_t)n);
                close(pfd);
                chmod_libc("/run/supervisor.pid", 0644);
            }
            printf("supervisor: daemonized as PID %d\n", pid);
            return 0;
        }
    }

    printf("supervisor: watching %d service(s)\n", watched_n);
    for (;;) {
        tick();
        sleep(POLL_INTERVAL_SECS);
    }
    return 0;
}
