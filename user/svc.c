/*
 * /sbin/svc -- LiteNix service controller.
 *
 * A minimal but real service supervisor in the spirit of OpenRC/s6/
 * runit: replaces the legacy shell wrapper with a C binary that
 *
 *   - parses /etc/services.available/<name>.conf definitions
 *     (KEY="VALUE" format, lines starting with # are comments)
 *   - records runtime state under /run/services/<name>.pid
 *   - records per-service logs under /var/log/services/<name>.log
 *   - understands start / stop / restart / status / enable / disable
 *   - understands dependency ordering via AFTER="..." (synchronous)
 *   - uses a C-implemented parser so it doesn't need bash to be present
 *
 * Service definition format:
 *   NAME="<command-name>"
 *   DESCRIPTION="<human readable>"
 *   EXEC="<argv[0] to exec>"
 *   ARGV="[<argv[1]>] ..."            (optional; default = none)
 *   AFTER="<svc1> <svc2> ..."          (optional; default = none)
 *   RESPAWN="yes|no"                  (optional; default = no)
 *   MAX_RESTARTS="<N>"                (optional; default = 5)
 *   LOG_FILE="<path>"                 (optional; default = /var/log/services/<name>.log)
 *   USER="<user>"                    (optional; placeholder for future)
 *
 * Layout under /:
 *   /etc/services.available/<name>.conf   (definitions)
 *   /etc/services.enabled                  (one <name> per line, what to start at boot)
 *   /run/services/<name>.pid               (running PID, or stale on crash)
 *   /var/log/services/<name>.log           (per-service stdout/stderr)
 *
 * Usage:
 *   svc list                          list known services + status
 *   svc <name> start                  start a service
 *   svc <name> stop                   stop a service (SIGTERM, then SIGKILL)
 *   svc <name> status                 show pid / state
 *   svc <name> enable                 add to /etc/services.enabled
 *   svc <name> disable                remove from /etc/services.enabled
 *   svc <name> restart                stop + start
 *
 * For now only UID 0 can run svc (other users can list). The
 * /etc/services.available files are owned root with mode 0644 so any
 * user can read them, but starting a service requires root.
 */

#include "libc_lite.h"
#include <stdbool.h>

#define SVC_DEF_DIR   "/etc/services.available"
#define SVC_ENABLED    "/etc/services.enabled"
#define SVC_RUN_DIR    "/run/services"
#define SVC_LOG_DIR    "/var/log/services"

#define SVC_MAX_LINE   512
#define SVC_MAX_KEY    64
#define SVC_MAX_VAL    384

struct svc_def {
    char  name[64];
    char  description[256];
    char  exec[256];
    char  argv[256];
    char  after[256];
    bool  respawn;
    int   max_restarts;
    char  log_file[256];
};

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

static int write_text_file(const char *path, const char *body)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    chmod_libc(path, 0644);
    size_t n = strlen(body);
    ssize_t w = write(fd, body, n);
    close(fd);
    return (w == (ssize_t)n) ? 0 : -1;
}

static void copy_field(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    size_t n = src_len;
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

/* Strip surrounding double-quotes if present. */
static void unquote_inplace(char *s)
{
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        memmove(s, s + 1, n - 2);
        s[n - 2] = 0;
    }
}

/* Parse one KEY="VALUE" line. Returns 0 on success, -1 on noise. */
static int parse_kv_line(char *line, char *out_key, char *out_val)
{
    /* strip leading whitespace */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == 0 || *line == '#' || *line == '\n') return -1;

    /* find '=' */
    char *eq = strchr(line, '=');
    if (eq == 0) return -1;

    /* extract key */
    char *key_end = eq;
    while (key_end > line && (key_end[-1] == ' ' || key_end[-1] == '\t')) key_end--;
    copy_field(out_key, SVC_MAX_KEY, line, (size_t)(key_end - line));

    /* extract value (start after '=' and skip ws) */
    char *val = eq + 1;
    while (*val == ' ' || *val == '\t') val++;
    /* trim trailing whitespace/newline */
    char *v_end = val + strlen(val);
    while (v_end > val && (v_end[-1] == '\n' || v_end[-1] == '\r' ||
                           v_end[-1] == ' ' || v_end[-1] == '\t')) {
        v_end--;
    }
    copy_field(out_val, SVC_MAX_VAL, val, (size_t)(v_end - val));
    unquote_inplace(out_val);
    return 0;
}

static int load_def(const char *name, struct svc_def *out)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.conf", SVC_DEF_DIR, name);

    char body[8192];
    if (read_text_file(path, body, sizeof(body)) < 0) return -1;

    memset(out, 0, sizeof(*out));
    /* Sensible defaults */
    snprintf(out->name, sizeof(out->name), "%s", name);
    snprintf(out->exec, sizeof(out->exec), "%s", name);
    out->respawn = false;
    out->max_restarts = 5;
    snprintf(out->log_file, sizeof(out->log_file), "%s/%s.log", SVC_LOG_DIR, name);

    char *line = body;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;

        char key[SVC_MAX_KEY], val[SVC_MAX_VAL];
        if (parse_kv_line(line, key, val) == 0) {
            if      (strcmp(key, "NAME") == 0)        copy_field(out->name, sizeof(out->name), val, strlen(val));
            else if (strcmp(key, "DESCRIPTION") == 0) copy_field(out->description, sizeof(out->description), val, strlen(val));
            else if (strcmp(key, "EXEC") == 0)        copy_field(out->exec, sizeof(out->exec), val, strlen(val));
            else if (strcmp(key, "ARGV") == 0)        copy_field(out->argv, sizeof(out->argv), val, strlen(val));
            else if (strcmp(key, "AFTER") == 0)       copy_field(out->after, sizeof(out->after), val, strlen(val));
            else if (strcmp(key, "RESPAWN") == 0)     out->respawn = (strcmp(val, "yes") == 0);
            else if (strcmp(key, "MAX_RESTARTS") == 0) out->max_restarts = atoi(val);
            else if (strcmp(key, "LOG_FILE") == 0)    copy_field(out->log_file, sizeof(out->log_file), val, strlen(val));
            /* else: unknown key — ignore for forward-compat */
        }

        if (nl) line = nl + 1; else break;
    }
    return 0;
}

static int def_path(const char *name, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/%s.conf", SVC_DEF_DIR, name);
    return 0;
}

static int pid_path(const char *name, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/%s.pid", SVC_RUN_DIR, name);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Service status (used by svc and supervisor)                          */
/* ------------------------------------------------------------------ */

enum svc_state {
    SVC_UNKNOWN = 0,
    SVC_STOPPED,
    SVC_RUNNING,
    SVC_CRASHED,        /* PID file exists but the pid is dead */
    SVC_DISABLED
};

static int read_pid_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    /* parse first integer */
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

/* check if pid is alive by reading /proc/<pid>/status. If we can open
 * the file, the pid is owned by some process. */
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

static enum svc_state status_of(const char *name)
{
    char path[256];
    pid_path(name, path, sizeof(path));
    int pid = read_pid_file(path);
    if (pid <= 0) return SVC_STOPPED;
    if (pid_alive(pid)) return SVC_RUNNING;
    return SVC_CRASHED;
}

/* ------------------------------------------------------------------ */
/* start / stop / status / restart                                     */
/* ------------------------------------------------------------------ */

static void split_words(const char *line, char *out, size_t out_size)
{
    /* Copy space-separated words verbatim. out_size includes NUL. */
    size_t i = 0;
    while (*line && i + 1 < out_size) {
        while (*line == ' ' || *line == '\t') line++;
        while (*line && *line != ' ' && *line != '\t' && i + 1 < out_size) {
            out[i++] = *line++;
        }
        if (*line == ' ' || *line == '\t') out[i++] = ' ';
    }
    out[i] = 0;
}

static int do_start(const struct svc_def *d)
{
    if (getuid() != 0) {
        printf("svc: only root can start services\n");
        return 1;
    }
    enum svc_state st = status_of(d->name);
    if (st == SVC_RUNNING) {
        printf("svc: %s is already running\n", d->name);
        return 0;
    }
    if (st == SVC_CRASHED) {
        /* stale PID file — overwrite */
        printf("svc: %s crashed last time, cleaning pid file\n", d->name);
    }

    /* Ensure runtime dirs exist. */
    mkdir(SVC_RUN_DIR, 0755);
    mkdir(SVC_LOG_DIR, 0755);

    /* Open the log file in append mode so the daemon's output is
     * preserved across restarts. */
    int logfd = open(d->log_file, O_WRONLY | O_CREAT | O_APPEND);
    if (logfd < 0) {
        printf("svc: cannot open log %s\n", d->log_file);
        return 1;
    }
    chmod_libc(d->log_file, 0644);

    int pid = fork();
    if (pid < 0) {
        printf("svc: fork failed\n");
        close(logfd);
        return 1;
    }
    if (pid == 0) {
        /* child: redirect stdio to log, exec the service */
        if (dup2(logfd, 1) < 0) _exit(126);
        if (dup2(logfd, 2) < 0) _exit(126);
        close(logfd);

        /* If ARGV is set, we want a precise argv. Otherwise exec the
         * single command in EXEC. */
        if (d->argv[0] == 0) {
            char *argv[] = { (char *)d->exec, 0 };
            char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", 0 };
            execve(d->exec, argv, envp);
        } else {
            /* ARGV is a flat string of words. Build a proper argv[] on
             * the stack by modifying the string in place — replace
             * spaces with NULs. */
            char argv_str[256];
            size_t n = strlen(d->argv);
            if (n >= sizeof(argv_str)) n = sizeof(argv_str) - 1;
            memcpy(argv_str, d->argv, n);
            argv_str[n] = 0;

            char *argvp[16] = { 0 };
            int argc = 0;
            argvp[argc++] = argv_str;
            char *p = argv_str;
            while (*p && argc < 15) {
                while (*p == ' ' || *p == '\t') { *p++ = 0; continue; }
                if (*p == 0) break;
                argvp[argc++] = p;
                while (*p && *p != ' ' && *p != '\t') p++;
            }
            argvp[argc] = 0;

            char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", 0 };
            execve(d->exec, argvp, envp);
        }
        _exit(127);
    }

    /* parent */
    close(logfd);

    /* Write the PID file synchronously so the supervisor / status
     * checks see the right pid right away. */
    char pp[64];
    pid_path(d->name, pp, sizeof(pp));
    int pfd = open(pp, O_WRONLY | O_CREAT | O_TRUNC);
    if (pfd >= 0) {
        int n = snprintf(pp, sizeof(pp), "%d\n", pid);
        if (n > 0) write(pfd, pp, (size_t)n);
        close(pfd);
        chmod_libc(pp, 0644);
    }

    /* Reap the child briefly so we don't leak a zombie if it exits
     * before the user runs status. wait4 with WNOHANG returns
     * immediately. */
    int status = 0;
    wait4(pid, &status, 1 /* WNOHANG */, 0);

    if (pid_alive(pid)) {
        printf("svc: %s started (pid %d)\n", d->name, pid);
        return 0;
    } else {
        printf("svc: %s exited immediately (status %d)\n", d->name, status);
        return 1;
    }
}

static int do_stop(const char *name)
{
    if (getuid() != 0) {
        printf("svc: only root can stop services\n");
        return 1;
    }
    char pp[256];
    pid_path(name, pp, sizeof(pp));
    int pid = read_pid_file(pp);
    if (pid <= 0) {
        printf("svc: %s is not running\n", name);
        return 0;
    }
    if (!pid_alive(pid)) {
        printf("svc: %s pid file stale, cleaning up\n", name);
        unlink(pp);
        return 0;
    }

    printf("svc: stopping %s (pid %d)\n", name, pid);
    kill(pid, SIGTERM);
    /* wait up to 3 seconds for graceful exit */
    for (int i = 0; i < 30; i++) {
        if (!pid_alive(pid)) break;
        /* 100ms sleep; we don't have usleep, but sleep(0) is
         * approximate. The scheduler tick is 10ms. */
        struct timespec ts = { 0, 100 * 1000 * 1000 };
        nanosleep(&ts, 0);
    }
    if (pid_alive(pid)) {
        printf("svc: %s did not exit in time, sending SIGKILL\n", name);
        kill(pid, SIGKILL);
    }
    /* Reap */
    int status = 0;
    wait4(pid, &status, 0, 0);
    unlink(pp);
    return 0;
}

static int do_status(const char *name)
{
    char path[256];
    pid_path(name, path, sizeof(path));
    int pid = read_pid_file(path);
    const char *state;
    if (pid > 0 && pid_alive(pid)) {
        state = "running";
        printf("svc: %-24s state=%-8s pid=%d\n", name, state, pid);
        return 0;
    } else if (pid > 0) {
        state = "crashed";
        printf("svc: %-24s state=%-8s (stale pid %d)\n", name, state, pid);
        return 1;
    } else {
        state = "stopped";
        printf("svc: %-24s state=%-8s\n", name, state);
        return 3;  /* matches systemd-ish convention */
    }
}

/* ------------------------------------------------------------------ */
/* describe <name> — show the parsed .conf + runtime state              */
/* ------------------------------------------------------------------ */

/* Format a bool as yes/no. Keeps the output one-word-per-line so a
 * caller can grep "enabled" or "respawn" without depending on
 * column alignment. */
static const char *yesno(bool b) { return b ? "yes" : "no"; }

static int is_enabled(const char *name)
{
    char body[4096];
    if (read_text_file(SVC_ENABLED, body, sizeof(body)) < 0) return 0;
    char *line = body;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (strcmp(line, name) == 0) return 1;
        if (nl) line = nl + 1; else break;
    }
    return 0;
}

static int do_describe(const char *name)
{
    struct svc_def d;
    if (load_def(name, &d) < 0) {
        printf("svc: %s: no such service definition in %s/\n", name, SVC_DEF_DIR);
        return 1;
    }

    /* Definition block. The fields are printed KEY=VALUE one per
     * line so the output is greppable. We always show every known
     * key (even if empty) so a downstream tool can parse it
     * without having to remember which ones are optional. */
    printf("svc: definition for %s\n", name);
    printf("  file          = %s/%s.conf\n", SVC_DEF_DIR, name);
    printf("  name          = %s\n", d.name[0] ? d.name : name);
    printf("  description   = %s\n", d.description[0] ? d.description : "(none)");
    printf("  exec          = %s\n", d.exec);
    printf("  argv          = %s\n", d.argv[0] ? d.argv : "(none)");
    printf("  after         = %s\n", d.after[0] ? d.after : "(none)");
    printf("  respawn       = %s\n", yesno(d.respawn));
    printf("  max_restarts  = %d\n", d.max_restarts);
    printf("  log_file      = %s\n", d.log_file[0] ? d.log_file : "(default)");

    /* Runtime block. Use the same status probe as do_status, plus
     * the enable flag and the on-disk pid file. */
    char pp[256];
    pid_path(name, pp, sizeof(pp));
    int pid = read_pid_file(pp);
    const char *state;
    if (pid > 0 && pid_alive(pid)) {
        state = "running";
    } else if (pid > 0) {
        state = "crashed";
    } else {
        state = "stopped";
    }
    printf("svc: runtime state for %s\n", name);
    printf("  enabled       = %s\n", yesno(is_enabled(name)));
    printf("  state         = %s\n", state);
    if (pid > 0) printf("  pid_file      = %s (%d)\n", pp, pid);
    else         printf("  pid_file      = %s (none)\n", pp);
    printf("  log_file_path = %s\n", d.log_file[0] ? d.log_file : "(default)");

    return 0;
}

/* ------------------------------------------------------------------ */
/* enable / disable                                                    */
/* ------------------------------------------------------------------ */

static int do_enable(const char *name)
{
    if (getuid() != 0) { printf("svc: only root can enable services\n"); return 1; }
    char dp[256];
    def_path(name, dp, sizeof(dp));
    int dfd = open(dp, O_RDONLY);
    if (dfd < 0) {
        printf("svc: %s: no such service definition (%s)\n", name, dp);
        return 1;
    }
    close(dfd);

    char body[4096];
    int n = read_text_file(SVC_ENABLED, body, sizeof(body));
    if (n < 0) body[0] = 0;

    /* Already enabled? */
    char *line = body;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (strcmp(line, name) == 0) {
            printf("svc: %s already enabled\n", name);
            return 0;
        }
        if (nl) line = nl + 1; else break;
    }

    char new_body[4200];
    int len = strlen(body);
    if (len > 0) {
        memcpy(new_body, body, len);
        new_body[len] = '\n';
        new_body[len + 1] = 0;
        len += 1;
    } else {
        new_body[0] = 0;
        len = 0;
    }
    memcpy(new_body + len, name, strlen(name));
    new_body[len + strlen(name)] = '\n';
    new_body[len + strlen(name) + 1] = 0;

    if (write_text_file(SVC_ENABLED, new_body) < 0) {
        printf("svc: cannot write %s\n", SVC_ENABLED);
        return 1;
    }
    printf("svc: %s enabled\n", name);
    return 0;
}

static int do_disable(const char *name)
{
    if (getuid() != 0) { printf("svc: only root can disable services\n"); return 1; }
    char body[4096];
    int n = read_text_file(SVC_ENABLED, body, sizeof(body));
    if (n < 0) {
        printf("svc: cannot read %s\n", SVC_ENABLED);
        return 1;
    }

    char out[4200];
    size_t op = 0;
    int removed = 0;
    char *line = body;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (strcmp(line, name) != 0) {
            size_t nln = strlen(line);
            if (op + nln + 2 < sizeof(out)) {
                memcpy(out + op, line, nln);
                op += nln;
                out[op++] = '\n';
            }
        } else {
            removed = 1;
        }
        if (nl) line = nl + 1; else break;
    }
    out[op] = 0;
    if (write_text_file(SVC_ENABLED, out) < 0) {
        printf("svc: cannot write %s\n", SVC_ENABLED);
        return 1;
    }
    if (removed) {
        printf("svc: %s disabled\n", name);
        return 0;
    } else {
        printf("svc: %s was not enabled\n", name);
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* tree — show the AFTER= dependency graph with runtime state            */
/* ------------------------------------------------------------------ */

/* Cap on the number of services we walk when building the tree.
 * The on-disk repo is small (a handful of services) so this is just
 * a safety net; if a real repo ever exceeds 64 we'd want to read
 * /etc/services.available/ via getdents into a dynamic buffer. */
#define TREE_MAX_SERVICES 64

/* A directed edge in the AFTER graph. We store it as a list of
 * `(depends_on_i, service_i)` tuples, walked from "no one depends
 * on me" roots downward. */
struct svc_dep_edge {
    int  from;       /* service index in tree[] that depends on `to` */
    int  to;         /* service index in tree[] that is depended on */
};

struct svc_tree_node {
    char name[64];
    bool defined;        /* .conf exists */
    bool enabled;        /* in /etc/services.enabled */
    char status[16];     /* running/stopped/crashed/unknown */
};

/* Read all .conf files into tree[]. `tree[]` is keyed by service
 * name (strcmp lookup). The AFTER= field is parsed into the
 * edges[] list. Returns the number of services found, or -1 on
 * error. */
static int tree_load(struct svc_tree_node tree[TREE_MAX_SERVICES],
                     struct svc_dep_edge edges[TREE_MAX_SERVICES * 4],
                     int *edge_count)
{
    int fd = open(SVC_DEF_DIR, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        printf("svc: cannot read %s\n", SVC_DEF_DIR);
        return -1;
    }

    char buf[16384];
    int n = getdents64(fd, (struct linux_dirent64 *)buf, sizeof(buf));
    close(fd);
    if (n < 0) {
        printf("svc: getdents64 failed\n");
        return -1;
    }

    int count = 0;
    int bpos = 0;
    while (bpos < n && count < TREE_MAX_SERVICES) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
        size_t nlen = strlen(d->d_name);
        if (nlen > 5 && strcmp(d->d_name + nlen - 5, ".conf") == 0) {
            size_t bl = nlen - 5;
            if (bl >= sizeof(tree[count].name)) bl = sizeof(tree[count].name) - 1;
            memcpy(tree[count].name, d->d_name, bl);
            tree[count].name[bl] = 0;
            tree[count].defined = true;
            /* defaults filled in after we know the full set so the
             * status probe works for any service name; the runtime
             * status is per-call though, so do that after building
             * the full name list. */
            count++;
        }
        bpos += d->d_reclen;
    }

    /* Pre-load the status for each service so the tree node carries
     * live data. The enabled flag is computed once at the end. */
    for (int i = 0; i < count; i++) {
        char pp[256];
        pid_path(tree[i].name, pp, sizeof(pp));
        int pid = read_pid_file(pp);
        if (pid > 0 && pid_alive(pid)) {
            snprintf(tree[i].status, sizeof(tree[i].status), "running");
        } else if (pid > 0) {
            snprintf(tree[i].status, sizeof(tree[i].status), "crashed");
        } else {
            snprintf(tree[i].status, sizeof(tree[i].status), "stopped");
        }
    }

    /* For each service, parse its AFTER= list and add edges. We
     * resolve the dependency name to an index in tree[]; if the
     * name doesn't match any loaded service we still record the
     * edge target as -1 and report a warning at print time. */
    *edge_count = 0;
    for (int i = 0; i < count && *edge_count < TREE_MAX_SERVICES * 4; i++) {
        struct svc_def d;
        if (load_def(tree[i].name, &d) != 0) continue;
        if (d.after[0] == 0) continue;
        char deps[256];
        snprintf(deps, sizeof(deps), "%s", d.after);
        char *tok = deps;
        while (*tok) {
            char *sp = strchr(tok, ' ');
            if (sp) { *sp = 0; }
            int target = -1;
            for (int j = 0; j < count; j++) {
                if (strcmp(tree[j].name, tok) == 0) { target = j; break; }
            }
            if (target >= 0) {
                edges[*edge_count].from = i;
                edges[*edge_count].to = target;
                (*edge_count)++;
            }
            if (sp) { *sp = ' '; tok = sp + 1; }
            else break;
        }
    }

    return count;
}

/* Recursive DFS print of a service and its dependents. We use
 * one `state[]` byte array: 0 = unseen, 1 = on current path,
 * 2 = fully visited. The 1-state is the cycle marker; we
 * refuse to recurse into it so a real cycle prints `[cycle]`
 * instead of looping forever. */
static void tree_print_recurse(int i,
                                const struct svc_tree_node tree[],
                                const struct svc_dep_edge edges[],
                                int edge_count,
                                int depth,
                                uint8_t state[])
{
    if (depth > 8) {
        printf("%*s[depth limit]\n", depth * 2, "");
        return;
    }
    if (state[i] == 1) {
        printf("%*s%s [cycle]\n", depth * 2, "", tree[i].name);
        return;
    }
    if (state[i] == 2) return;
    state[i] = 1;

    /* Per-node status: enabled flag + running/stopped/crashed.
     * The status string is annotated with `(no .conf)` if the
     * node was a dependency target that didn't have its own
     * .conf — a useful "is something missing?" signal. */
    if (!tree[i].defined) {
        printf("%*s%s (no .conf)\n", depth * 2, "", tree[i].name);
    } else {
        printf("%*s%s [%s]%s\n",
               depth * 2, "", tree[i].name, tree[i].status,
               tree[i].enabled ? "  (enabled at boot)" : "");
    }

    for (int e = 0; e < edge_count; e++) {
        if (edges[e].from == i) {
            tree_print_recurse(edges[e].to, tree, edges, edge_count,
                                depth + 1, state);
        }
    }
    state[i] = 2;
}

static int do_tree(void)
{
    struct svc_tree_node tree[TREE_MAX_SERVICES];
    struct svc_dep_edge edges[TREE_MAX_SERVICES * 4];
    int edge_count = 0;
    int count = tree_load(tree, edges, &edge_count);
    if (count < 0) return 1;
    if (count == 0) {
        printf("svc: no services in %s\n", SVC_DEF_DIR);
        return 0;
    }

    /* Mark each service as enabled/disabled. We do this once,
     * after load, so the recursion only has to read one file. */
    char body[4096];
    bool have_enabled = (read_text_file(SVC_ENABLED, body, sizeof(body)) >= 0);
    for (int i = 0; i < count; i++) {
        tree[i].enabled = false;
        if (!have_enabled) continue;
        char *line = body;
        while (line && *line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = 0;
            if (strcmp(line, tree[i].name) == 0) tree[i].enabled = true;
            if (nl) line = nl + 1; else break;
        }
    }

    /* Roots are services nobody depends on. We don't sort: the
     * tmpfs in this codebase returns getdents in a stable order,
     * and there are at most a handful of services. */
    bool is_root[TREE_MAX_SERVICES];
    for (int i = 0; i < count; i++) is_root[i] = true;
    for (int e = 0; e < edge_count; e++) {
        if (edges[e].to >= 0 && edges[e].to < count) {
            is_root[edges[e].to] = false;
        }
    }

    /* 0 = unseen, 1 = on current DFS path, 2 = fully visited. */
    uint8_t state[TREE_MAX_SERVICES] = {0};
    printf("svc: dependency tree (root services at top):\n");
    for (int i = 0; i < count; i++) {
        if (is_root[i]) {
            tree_print_recurse(i, tree, edges, edge_count, 0, state);
        }
    }

    /* Anything that didn't reach state 2 (fully visited) is part
     * of a cycle or unreachable. Print it under a single header
     * so the operator can spot it. */
    bool any_unvisited = false;
    for (int i = 0; i < count; i++) {
        if (state[i] != 2) {
            if (!any_unvisited) {
                printf("svc: [unreachable / cyclic] nodes:\n");
                any_unvisited = true;
            }
            printf("  %s [%s]%s\n", tree[i].name, tree[i].status,
                   tree[i].enabled ? "  (enabled at boot)" : "");
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* list                                                                */
/* ------------------------------------------------------------------ */

static int do_list(void)
{
    int fd = open(SVC_DEF_DIR, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        printf("svc: cannot read %s\n", SVC_DEF_DIR);
        return 1;
    }
    char buf[16384];
    int n = getdents64(fd, (struct linux_dirent64 *)buf, sizeof(buf));
    close(fd);
    if (n < 0) {
        printf("svc: getdents64 failed\n");
        return 1;
    }

    /* gather enabled set for marking */
    char enabled_body[4096];
    bool has_enabled = (read_text_file(SVC_ENABLED, enabled_body,
                                        sizeof(enabled_body)) >= 0);

    int bpos = 0;
    int shown = 0;
    while (bpos < n) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
        size_t nlen = strlen(d->d_name);
        if (nlen > 5 && strcmp(d->d_name + nlen - 5, ".conf") == 0) {
            char name[64];
            size_t bl = nlen - 5;
            if (bl >= sizeof(name)) bl = sizeof(name) - 1;
            memcpy(name, d->d_name, bl);
            name[bl] = 0;
            bool en = false;
            if (has_enabled) {
                char *line = enabled_body;
                while (line && *line) {
                    char *nl = strchr(line, '\n');
                    if (nl) *nl = 0;
                    if (strcmp(line, name) == 0) en = true;
                    if (nl) line = nl + 1; else break;
                }
            }
            int rc = do_status(name);
            if (en) printf("        (enabled at boot)\n");
            (void)rc;
            shown++;
        }
        bpos += d->d_reclen;
    }
    if (shown == 0) printf("svc: no services in %s\n", SVC_DEF_DIR);
    return 0;
}

/* ------------------------------------------------------------------ */
/* start-all (used by rcS.sh)                                          */
/* ------------------------------------------------------------------ */

static int do_start_enabled(void)
{
    if (getuid() != 0) { printf("svc: only root can start services\n"); return 1; }
    char body[4096];
    if (read_text_file(SVC_ENABLED, body, sizeof(body)) < 0) {
        printf("svc: cannot read %s\n", SVC_ENABLED);
        return 1;
    }

    /* Parse enabled list into an array. We respect AFTER="..." ordering
     * within one pass by re-scanning the list until no new service is
     * started; a service whose AFTER dependency isn't yet RUNNING is
     * deferred. This implements single-level dependency ordering. */
    int started = 0, deferred = 0, total = 0;
    int pass_limit = 32;
    while (pass_limit-- > 0) {
        int made_progress = 0;
        char *line = body;
        char *snapshot = body;
        while (line && *line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = 0;
            if (line[0]) {
                total++;
                enum svc_state st = status_of(line);
                if (st == SVC_RUNNING) { line = nl ? nl + 1 : 0; continue; }

                struct svc_def d;
                if (load_def(line, &d) == 0) {
                    bool deps_ok = true;
                    if (d.after[0]) {
                        char deps[256];
                        split_words(d.after, deps, sizeof(deps));
                        char *tok = deps;
                        while (*tok) {
                            char save = 0;
                            char *sp = strchr(tok, ' ');
                            if (sp) { save = *sp; *sp = 0; }
                            enum svc_state ds = status_of(tok);
                            if (ds != SVC_RUNNING) deps_ok = false;
                            if (sp) { *sp = save; tok = sp + 1; }
                            else break;
                        }
                    }
                    if (deps_ok) {
                        if (do_start(&d) == 0) { started++; made_progress = 1; }
                    } else {
                        deferred++;
                    }
                }
            }
            if (nl) line = nl + 1; else break;
        }
        (void)snapshot;
        if (!made_progress) break;
    }
    printf("svc: started=%d deferred=%d total=%d\n", started, deferred, total);
    return deferred == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* check <name>|all — validate on-disk service state                    */
/* ------------------------------------------------------------------ */

/* Per-service check report. We collect issues as a small
 * bitfield-style counter plus the names of the offending
 * targets, so the printer at the bottom can decide how loud to
 * be. We don't bother allocating dynamically — TREE_MAX_SERVICES
 * bounds the depth, and the AFTER= list is short. */
#define SVC_CHECK_OK              0
#define SVC_CHECK_CONF_MISSING   (1 << 0)   /* .conf parse failed */
#define SVC_CHECK_EXEC_MISSING   (1 << 1)   /* EXEC= path doesn't exist */
#define SVC_CHECK_DEP_UNDEFINED  (1 << 2)   /* AFTER= name not in services.available */
#define SVC_CHECK_ORPHAN_ENABLE  (1 << 3)   /* enabled without a .conf */

static void check_report(const char *name, int issues, const char *details)
{
    if (issues == SVC_CHECK_OK) {
        printf("svc: %-24s OK\n", name);
        return;
    }
    printf("svc: %-24s FAIL", name);
    if (issues & SVC_CHECK_CONF_MISSING)   printf(" [conf parse error]");
    if (issues & SVC_CHECK_EXEC_MISSING)   printf(" [exec missing: %s]", details);
    if (issues & SVC_CHECK_DEP_UNDEFINED)  printf(" [undefined dep: %s]", details);
    if (issues & SVC_CHECK_ORPHAN_ENABLE)  printf(" [orphan enable: %s]", details);
    printf("\n");
}

/* Run all the per-service checks for a single service. Returns
 * a bitfield of SVC_CHECK_* flags. If `out_detail` is non-null
 * it's filled in with the first problematic detail for the
 * printer to use. */
static int check_service(const char *name, char *out_detail, size_t detail_size)
{
    if (out_detail && detail_size > 0) out_detail[0] = 0;

    struct svc_def d;
    if (load_def(name, &d) != 0) {
        return SVC_CHECK_CONF_MISSING;
    }

    /* stat() catches both "exec doesn't exist" and "exec is an
     * empty path" (the loader would have rejected the latter
     * at parse time, but the check is cheap). */
    struct stat st;
    if (stat(d.exec, &st) != 0) {
        if (out_detail) snprintf(out_detail, detail_size, "%s", d.exec);
        return SVC_CHECK_EXEC_MISSING;
    }

    /* AFTER= dependencies: every name must resolve to a service
     * in /etc/services.available/. We do a one-pass scan against
     * the directory's getdents output. */
    if (d.after[0] != 0) {
        int fd = open(SVC_DEF_DIR, O_RDONLY | O_DIRECTORY);
        if (fd >= 0) {
            char buf[8192];
            int n = getdents64(fd, (struct linux_dirent64 *)buf, sizeof(buf));
            close(fd);
            if (n > 0) {
                char deps[256];
                snprintf(deps, sizeof(deps), "%s", d.after);
                char *tok = deps;
                while (*tok) {
                    char *sp = strchr(tok, ' ');
                    if (sp) *sp = 0;
                    bool found = false;
                    int bpos = 0;
                    while (bpos < n) {
                        struct linux_dirent64 *de = (struct linux_dirent64 *)(buf + bpos);
                        size_t nlen = strlen(de->d_name);
                        if (nlen > 5 && strcmp(de->d_name + nlen - 5, ".conf") == 0
                            && strncmp(de->d_name, tok, nlen - 5) == 0
                            && de->d_name[nlen - 5] == 0) {
                            found = true;
                            break;
                        }
                        bpos += de->d_reclen;
                    }
                    if (!found) {
                        if (out_detail) snprintf(out_detail, detail_size, "%s", tok);
                        if (sp) { *sp = ' '; tok = sp + 1; }
                        else break;
                        return SVC_CHECK_DEP_UNDEFINED;
                    }
                    if (sp) { *sp = ' '; tok = sp + 1; }
                    else break;
                }
            }
        }
    }

    return SVC_CHECK_OK;
}

/* Read every .conf name in SVC_DEF_DIR into `names[]` using
 * getdents. We slice the trailing ".conf" off each name. */
static int read_conf_names(char names[][64], int max)
{
    int fd = open(SVC_DEF_DIR, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return -1;
    char buf[16384];
    int n = getdents64(fd, (struct linux_dirent64 *)buf, sizeof(buf));
    close(fd);
    if (n < 0) return -1;
    int count = 0;
    int bpos = 0;
    while (bpos < n && count < max) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
        size_t nlen = strlen(d->d_name);
        if (nlen > 5 && strcmp(d->d_name + nlen - 5, ".conf") == 0) {
            size_t bl = nlen - 5;
            if (bl >= 64) bl = 63;
            memcpy(names[count], d->d_name, bl);
            names[count][bl] = 0;
            count++;
        }
        bpos += d->d_reclen;
    }
    return count;
}

/* `svc check [name]` (default: all) validates the on-disk
 * service state without starting anything. With a name, only
 * that service is checked; otherwise every .conf in
 * /etc/services.available/ is checked, and /etc/services.enabled
 * is cross-checked for orphan enables. */
static int do_check(const char *arg)
{
    char names[TREE_MAX_SERVICES][64];
    int count = 0;
    bool check_all = (arg == 0 || arg[0] == 0 || strcmp(arg, "all") == 0);
    if (check_all) {
        count = read_conf_names(names, TREE_MAX_SERVICES);
        if (count < 0) {
            printf("svc: cannot read %s\n", SVC_DEF_DIR);
            return 1;
        }
    } else {
        size_t nl = strlen(arg);
        if (nl >= 64) nl = 63;
        memcpy(names[0], arg, nl);
        names[0][nl] = 0;
        count = 1;
    }

    if (count == 0) {
        printf("svc: no services to check\n");
        return 0;
    }

    int fail_count = 0;
    for (int i = 0; i < count; i++) {
        char detail[128];
        int issues = check_service(names[i], detail, sizeof(detail));
        if (issues == SVC_CHECK_OK) {
            printf("svc: %-24s OK\n", names[i]);
        } else {
            check_report(names[i], issues, detail);
            fail_count++;
        }
    }

    /* Cross-check: enabled services without a matching .conf.
     * Only meaningful when we walked the whole repo ("all");
     * for a single-name probe we already checked that one. */
    if (check_all) {
        char body[4096];
        if (read_text_file(SVC_ENABLED, body, sizeof(body)) >= 0) {
            char *line = body;
            while (line && *line) {
                char *nl = strchr(line, '\n');
                if (nl) *nl = 0;
                if (line[0]) {
                    /* Cheap O(n*m) scan — n and m are both
                     * bounded by TREE_MAX_SERVICES so this stays
                     * under 4k comparisons even on a big repo. */
                    bool found = false;
                    for (int i = 0; i < count; i++) {
                        if (strcmp(names[i], line) == 0) {
                            found = true; break;
                        }
                    }
                    if (!found) {
                        char dp[256];
                        def_path(line, dp, sizeof(dp));
                        struct stat st;
                        if (stat(dp, &st) != 0) {
                            check_report(line, SVC_CHECK_ORPHAN_ENABLE, dp);
                            fail_count++;
                        }
                    }
                }
                if (nl) line = nl + 1; else break;
            }
        }
    }

    if (fail_count == 0) {
        printf("svc: check OK (%d service%s)\n",
               count, count == 1 ? "" : "s");
        return 0;
    }
    printf("svc: check found %d issue%s\n",
           fail_count, fail_count == 1 ? "" : "s");
    return 1;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void usage(void)
{
    printf("Usage: svc <command> [service]\n"
           "\n"
           "Commands:\n"
           "  list                       list known services + status\n"
           "  <svc> start                start a service\n"
           "  <svc> stop                 stop a service (SIGTERM, then SIGKILL)\n"
           "  <svc> status               show state and pid\n"
           "  <svc> restart              stop + start\n"
           "  <svc> log                  dump the per-service log (max 64 KiB)\n"
           "  <svc> describe             show the parsed .conf + runtime state\n"
           "  <svc> enable               add to /etc/services.enabled\n"
           "  <svc> disable              remove from /etc/services.enabled\n"
           "  start-enabled              start everything in /etc/services.enabled\n"
           "  tree                        show the AFTER= dependency graph with state\n"
           "  check [name]                validate .conf / EXEC / deps (default: all)\n"
           "  help                       this text\n");
}

/* ------------------------------------------------------------------ */
/* log <name> — read the per-service log file                          */
/* ------------------------------------------------------------------ */

/* Read every byte of `path` into stdout, in 1 KiB chunks. Returns 0
 * on success, 1 if the file could not be opened, 2 if the read
 * failed. A non-existent file is not an error here: the per-service
 * log only exists after the service has been started at least once
 * (svc start opens it in O_CREAT). We report "no log yet" with a
 * clear message and exit 3 (the same "no data" code that status
 * uses) so a script can tell a quiet service from a missing one. */
static int do_log(const char *name)
{
    /* Read the .conf first so the user can override LOG_FILE per
     * service; fall back to the default location if not set. */
    struct svc_def d;
    char log_path[300];
    if (load_def(name, &d) == 0 && d.log_file[0] != 0) {
        snprintf(log_path, sizeof(log_path), "%s", d.log_file);
    } else {
        snprintf(log_path, sizeof(log_path), "%s/%s.log", SVC_LOG_DIR, name);
    }

    int fd = open(log_path, O_RDONLY);
    if (fd < 0) {
        /* We can't ask the kernel for ENOENT explicitly (libc-lite
         * has no errno yet), so we probe with stat(). A non-existent
         * file gets "no log yet"; anything else is a real error. */
        struct stat st;
        if (stat(log_path, &st) != 0) {
            printf("svc: %s has no log yet (try `svc %s start` first)\n",
                   name, name);
            return 3;
        }
        printf("svc: cannot open %s\n", log_path);
        return 1;
    }

    /* Stream the file to stdout in chunks. A 64 KiB cap on a single
     * dump is plenty for the small daemons we ship and prevents an
     * accidental /dev/zero tail from flooding the terminal; callers
     * who want the full file can use the shell's `cat` once we have
     * one. */
    char buf[1024];
    ssize_t total = 0;
    const ssize_t max_dump = 64 * 1024;
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            printf("\nsvc: read error on %s\n", log_path);
            close(fd);
            return 2;
        }
        if (n == 0) break;
        ssize_t writable = n;
        if (total + writable > max_dump) {
            writable = max_dump - total;
            if (writable <= 0) {
                printf("\nsvc: %s: log truncated at %d bytes (use `cat` for full dump)\n",
                       log_path, max_dump);
                break;
            }
        }
        (void)write(1, buf, (size_t)writable);
        total += writable;
        if (writable < n) {
            printf("\nsvc: %s: log truncated at %d bytes (use `cat` for full dump)\n",
                   log_path, max_dump);
            /* drain the rest of the file we skipped so the file
             * descriptor doesn't sit open with unread data forever */
            char drain[256];
            while (read(fd, drain, sizeof(drain)) > 0) { /* discard */ }
            break;
        }
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(); return 1; }

    if (strcmp(argv[1], "list") == 0) {
        return do_list();
    }
    if (strcmp(argv[1], "help") == 0) {
        usage();
        return 0;
    }
    if (strcmp(argv[1], "start-enabled") == 0) {
        return do_start_enabled();
    }
    if (strcmp(argv[1], "tree") == 0) {
        return do_tree();
    }
    if (strcmp(argv[1], "check") == 0) {
        return do_check(argc >= 3 ? argv[2] : "all");
    }
    if (argc < 3) {
        printf("svc: missing service name\n");
        usage();
        return 1;
    }
    const char *name = argv[2];
    const char *cmd = argv[1];

    if (strcmp(cmd, "log") == 0) {
        return do_log(name);
    }
    if (strcmp(cmd, "describe") == 0) {
        return do_describe(name);
    }
    if (strcmp(cmd, "start") == 0) {
        struct svc_def d;
        if (load_def(name, &d) < 0) {
            printf("svc: no such service %s\n", name);
            return 1;
        }
        return do_start(&d);
    }
    if (strcmp(cmd, "stop") == 0)  return do_stop(name);
    if (strcmp(cmd, "status") == 0) return do_status(name);
    if (strcmp(cmd, "restart") == 0) {
        do_stop(name);
        struct svc_def d;
        if (load_def(name, &d) < 0) { printf("svc: no such service %s\n", name); return 1; }
        return do_start(&d);
    }
    if (strcmp(cmd, "enable") == 0)  return do_enable(name);
    if (strcmp(cmd, "disable") == 0) return do_disable(name);

    printf("svc: unknown command '%s'\n", cmd);
    usage();
    return 1;
}
