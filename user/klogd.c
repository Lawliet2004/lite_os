/*
 * /sbin/klogd -- LiteNix kernel log daemon.
 *
 * Reads bytes from the kernel ring buffer exposed at /dev/kmsg and
 * appends them to /var/log/kern.log, prepending a timestamp. Mirrors the
 * behaviour of a real distribution's klogd: the in-kernel log is small
 * (16 KiB), so userspace must persist the stream or it gets lost.
 *
 * The current position in the ring is tracked with lseek + the file's
 * offset. On every iteration we read up to a small chunk, append to the
 * log file, and lseek forward. The log file is created if it does not
 * exist and is opened in append mode so it is safe to truncate from
 * outside (e.g. `logrotate`) without breaking the daemon.
 *
 * Time stamps come from CLOCK_REALTIME via gettimeofday(). They are
 * formatted as `[YYYY-MM-DD HH:MM:SS] ` and prepended to every chunk of
 * new data we read from the kmsg buffer (one timestamp per write batch,
 * not per byte, to keep the daemon overhead low).
 *
 * Usage:
 *   klogd                  -> run in foreground (used by init for testing)
 *   klogd --daemon         -> fork into the background (rcS.sh)
 *   klogd --once           -> drain whatever is currently in the kmsg
 *                            buffer and exit
 */

#include "libc_lite.h"
#include <stdbool.h>

#define KLOGD_CHUNK    4096
#define KLOGD_PATH     "/var/log/kern.log"
#define KLOGD_SOURCE   "/dev/kmsg"

/* civil_from_days: days since 1970-01-01 UTC -> Y-M-D (UTC).
 * Howard Hinnant's algorithm -- O(1), no loops, no table, no libc. */
static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;                                 /* [0, 146096] */
    int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365; /* [0, 399]    */
    int64_t yy  = yoe + era * 400;
    int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);               /* [0, 365]    */
    int64_t mp  = (5*doy + 2) / 153;                               /* [0, 11]     */
    int64_t dd  = doy - (153*mp + 2)/5 + 1;                        /* [1, 31]     */
    int64_t mm  = mp + (mp < 10 ? 3 : -9);                         /* [1, 12]     */
    yy += (mm <= 2 ? 1 : 0);
    *y = (int)yy;
    *m = (int)mm;
    *d = (int)dd;
}

static void format_timestamp(char *out, size_t out_size)
{
    struct timeval tv;
    if (gettimeofday(&tv, 0) != 0) {
        snprintf(out, out_size, "[unknown] ");
        return;
    }
    int64_t secs = (int64_t)tv.tv_sec;
    int64_t days = secs / 86400;
    int64_t rem  = secs - days * 86400;
    if (rem < 0) { rem += 86400; days -= 1; }
    int hr  = (int)(rem / 3600);
    int mi  = (int)((rem / 60) % 60);
    int sec = (int)(rem % 60);
    int yr, mo, day;
    civil_from_days(days, &yr, &mo, &day);
    snprintf(out, out_size, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] ",
             yr, mo, day, hr, mi, sec, (long)(tv.tv_usec / 1000));
}

/* Self-check: known epoch values -> known Y-M-D H:M:S.
 * Run with `klogd --self-test` after a build to verify the algorithm. */
static int run_timestamp_self_test(void)
{
    struct { int64_t e; int y,mo,d,h,mi,s; const char *label; } cases[] = {
        {          0LL, 1970, 1, 1,  0, 0,  0, "epoch"               },
        {     86400LL, 1970, 1, 2,  0, 0,  0, "+1d"                 },
        { 951782400LL, 2000, 2,29,  0, 0,  0, "2000 leap day"       },
        { 1705322096LL,2024, 1,15, 12,34, 56, "2024-01-15 12:34:56" },
        { 4107542400LL,2100, 2,28,  0, 0,  0, "2100 not leap"       },
        { 2147483647LL,2038, 1,19,  3,14,  7, "Y2038 max int32"     },
        {         -1LL,1969,12,31, 23,59, 59, "-1s pre-epoch"       },
    };
    int n = (int)(sizeof(cases) / sizeof(cases[0]));
    int ok = 1;
    for (int i = 0; i < n; i++) {
        int yr, mo, day;
        int64_t days = cases[i].e / 86400;
        int64_t rem  = cases[i].e - days * 86400;
        if (rem < 0) { rem += 86400; days -= 1; }
        int hr  = (int)(rem / 3600);
        int mi  = (int)((rem / 60) % 60);
        int sec = (int)(rem % 60);
        civil_from_days(days, &yr, &mo, &day);
        int pass = (yr == cases[i].y) && (mo == cases[i].mo) && (day == cases[i].d)
                && (hr == cases[i].h) && (mi == cases[i].mi) && (sec == cases[i].s);
        if (!pass) ok = 0;
        printf("  [%s] %-22s epoch=%-12lld got %04d-%02d-%02d %02d:%02d:%02d\n",
               pass ? " OK " : "FAIL",
               cases[i].label, (long long)cases[i].e,
               yr, mo, day, hr, mi, sec);
    }
    return ok;
}

/* Read all currently-available bytes from /dev/kmsg and append them to
 * the log file. Returns the number of bytes written, or -1 on error. */
static int drain_once(int log_fd, int src_fd, off_t *pos)
{
    if (lseek(src_fd, *pos, SEEK_SET) < 0) return -1;
    char buf[KLOGD_CHUNK];
    ssize_t n = read(src_fd, buf, sizeof(buf));
    if (n < 0) return -1;
    if (n == 0) return 0;

    char ts[40];
    format_timestamp(ts, sizeof(ts));
    if (write(log_fd, ts, strlen(ts)) != (ssize_t)strlen(ts)) return -1;
    if (write(log_fd, buf, (size_t)n) != n) return -1;

    *pos += n;
    return (int)n;
}

int main(int argc, char **argv)
{
    bool daemonize = false;
    bool once      = false;
    bool self_test = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--daemon") == 0) daemonize = true;
        else if (strcmp(argv[i], "--once") == 0) once = true;
        else if (strcmp(argv[i], "--self-test") == 0) self_test = true;
        else { printf("Usage: klogd [--daemon|--once|--self-test]\n"); return 1; }
    }
    if (self_test) {
        printf("klogd: timestamp self-test\n");
        return run_timestamp_self_test() == 1 ? 0 : 1;
    }

    /* Ensure the log directory exists. The initramfs already creates
     * /var/log, but a fresh install might not have. */
    mkdir("/var", 0755);
    mkdir("/var/log", 0755);

    int log_fd = open(KLOGD_PATH, O_WRONLY | O_CREAT);
    if (log_fd < 0) {
        printf("klogd: cannot open %s for writing\n", KLOGD_PATH);
        return 1;
    }
    /* Append semantics: position at the end of whatever is already there.
     * This means the first daemonization creates the file; subsequent
     * runs (or `klogd --once` invocations) keep appending. */
    if (lseek(log_fd, 0, SEEK_END) < 0) {
        close(log_fd);
        return 1;
    }
    /* Lock the log file down: owner rw, group r, other r. Real distros
     * go further (e.g. 0640 + syslog group); 0644 is enough for now. */
    chmod_libc(KLOGD_PATH, 0644);

    int src_fd = open(KLOGD_SOURCE, O_RDONLY);
    if (src_fd < 0) {
        printf("klogd: cannot open %s\n", KLOGD_SOURCE);
        close(log_fd);
        return 1;
    }

    if (daemonize) {
        int pid = fork();
        if (pid < 0) {
            printf("klogd: fork failed\n");
            close(log_fd); close(src_fd);
            return 1;
        }
        if (pid > 0) {
            /* Parent: log our PID file then exit. */
            int pidfd = open("/run/klogd.pid", O_WRONLY | O_CREAT | O_TRUNC);
            if (pidfd >= 0) {
                char pbuf[16];
                int n = snprintf(pbuf, sizeof(pbuf), "%d\n", pid);
                if (n > 0) write(pidfd, pbuf, (size_t)n);
                close(pidfd);
            }
            printf("klogd: daemonized as PID %d\n", pid);
            close(log_fd);
            close(src_fd);
            return 0;
        }
    }

    /* Seed our position at the end of the kernel ring buffer so we
     * don't dump the entire pre-existing history on every boot. If
     * the file has been truncated/replaced, the ring buffer's offset
     * will be past the start, and reading returns nothing for that gap. */
    off_t pos = lseek(src_fd, 0, SEEK_END);
    if (pos < 0) pos = 0;

    printf("klogd: starting at offset %ld\n", (long)pos);

    if (once) {
        int n = drain_once(log_fd, src_fd, &pos);
        if (n < 0) { printf("klogd: drain failed\n"); return 1; }
        printf("klogd: drained %d bytes (now at offset %ld)\n", n, (long)pos);
        close(log_fd);
        close(src_fd);
        return 0;
    }

    for (;;) {
        int n = drain_once(log_fd, src_fd, &pos);
        if (n < 0) {
            printf("klogd: read failed, exiting\n");
            break;
        }
        sleep(1);
    }

    close(log_fd);
    close(src_fd);
    return 0;
}
