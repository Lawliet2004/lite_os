/*
 * /sbin/useradd and /sbin/userdel -- LiteNix account management.
 *
 * Usage:
 *   useradd <name>                -- create a locked account
 *   useradd -u <uid> <name>       -- specify explicit UID
 *   useradd -p <password> <name>  -- set initial password directly
 *   userdel <name>                -- remove the account (does not delete $HOME)
 *
 * Adds entries to /etc/passwd, /etc/group and /etc/shadow.
 * Creates /home/<name> with mode 0700 owned by the new user.
 */

#include "libc_lite.h"

static uint32_t next_free_uid(void)
{
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd < 0) return 1000;
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 1000;
    buf[n] = 0;
    uint32_t hi = 999;
    char *line = buf;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (line[0] && line[0] != '#') {
            char tmp[512];
            strncpy(tmp, line, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = 0;
            char *p = tmp;
            (void)strsep_inplace(&p, ':');
            (void)strsep_inplace(&p, ':');
            char *uid = strsep_inplace(&p, ':');
            if (uid) {
                uint32_t u = (uint32_t)atoi(uid);
                if (u >= 1000 && u > hi) hi = u;
            }
        }
        if (!nl) break;
        line = nl + 1;
    }
    return hi + 1;
}

static int append_line(const char *path, const char *line)
{
    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT);
    if (fd < 0) return -1;
    ssize_t w = write(fd, line, strlen(line));
    close(fd);
    return w == (ssize_t)strlen(line) ? 0 : -1;
}

static int remove_user_from_file(const char *path, const char *name)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;

    char out[1024];
    size_t op = 0;
    int found = 0;
    char *line = buf;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        const char *colon = line;
        while (*colon && *colon != ':') colon++;
        size_t nlen = (size_t)(colon - line);
        if (strlen(name) == nlen && strncmp(line, name, nlen) == 0) {
            found = 1;
        } else if (line[0]) {
            op += snprintf(out + op, sizeof(out) - op, "%s\n", line);
        }
        if (!nl) break;
        line = nl + 1;
    }
    if (!found) return 0;
    int wfd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (wfd < 0) return -1;
    ssize_t w = write(wfd, out, op);
    close(wfd);
    return w == (ssize_t)op ? 0 : -1;
}

static int cmd_useradd(int argc, char **argv)
{
    if (geteuid() != 0) { printf("useradd: only root can add users\n"); return 1; }
    uint32_t uid = 0; int explicit_uid = 0;
    const char *initial_password = 0;
    const char *name = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) { uid = (uint32_t)atoi(argv[i + 1]); explicit_uid = 1; i++; }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) { initial_password = argv[i + 1]; i++; }
        else if (argv[i][0] != '-') name = argv[i];
    }
    if (!name) { printf("Usage: useradd [-u <uid>] [-p <password>] <name>\n"); return 1; }

    struct passwd_ent existing;
    if (pwent_by_name(name, &existing) == 0) { printf("useradd: user '%s' already exists\n", name); return 1; }

    if (!explicit_uid) uid = next_free_uid();
    uint32_t gid = uid;

    char home[160];
    snprintf(home, sizeof(home), "/home/%s", name);
    mkdir("/home", 0755);
    mkdir(home, 0700);

    char passwd_line[512];
    snprintf(passwd_line, sizeof(passwd_line), "%s:x:%u:%u:%s:%s:/bin/sh\n", name, uid, gid, name, home);
    if (append_line("/etc/passwd", passwd_line) != 0) { printf("useradd: failed to write /etc/passwd\n"); return 1; }

    char group_line[256];
    snprintf(group_line, sizeof(group_line), "%s:x:%u:\n", name, gid);
    append_line("/etc/group", group_line);

    char shadow_hash[160];
    if (initial_password) {
        char salt[10];
        gen_salt(salt, sizeof(salt));
        pw_hash(initial_password, salt, shadow_hash, sizeof(shadow_hash));
    } else {
        strncpy(shadow_hash, "!", sizeof(shadow_hash));
    }
    char shadow_line[256];
    snprintf(shadow_line, sizeof(shadow_line), "%s:%s:0:0:99999:7:::\n", name, shadow_hash);
    if (append_line("/etc/shadow", shadow_line) != 0) { printf("useradd: failed to write /etc/shadow\n"); return 1; }

    printf("useradd: created user '%s' (uid=%u, home=%s)%s\n", name, uid, home,
           initial_password ? "" : " — account is locked, use `passwd` to set a password");
    return 0;
}

static int cmd_userdel(int argc, char **argv)
{
    if (geteuid() != 0) { printf("userdel: only root can remove users\n"); return 1; }
    const char *name = 0;
    for (int i = 1; i < argc; i++) if (argv[i][0] != '-') name = argv[i];
    if (!name) { printf("Usage: userdel <name>\n"); return 1; }
    if (strcmp(name, "root") == 0) { printf("userdel: refusing to delete root\n"); return 1; }

    struct passwd_ent ex;
    if (pwent_by_name(name, &ex) != 0) { printf("userdel: user '%s' does not exist\n", name); return 1; }

    int e1 = remove_user_from_file("/etc/passwd", name);
    int e2 = remove_user_from_file("/etc/shadow", name);
    int e3 = remove_user_from_file("/etc/group",  name);
    if (e1 || e2 || e3) { printf("userdel: errors occurred while updating account files\n"); return 1; }
    printf("userdel: removed user '%s' (home directory %s left in place)\n", name, ex.home);
    return 0;
}

int main(int argc, char **argv)
{
    const char *prog = argv[0] ? argv[0] : "useradd";
    const char *base = prog;
    for (const char *s = prog; *s; s++) if (*s == '/' || *s == '\\') base = s + 1;
    if (strcmp(base, "userdel") == 0) return cmd_userdel(argc, argv);
    return cmd_useradd(argc, argv);
}
