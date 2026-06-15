/*
 * /bin/id, /bin/whoami, /bin/groups (multi-call binary)
 *
 * Behaviour:
 *   - id              -> "uid=<n>(<name>) gid=<n>(<name>) groups=<n>(<name>)"
 *   - id -u           -> effective uid as number
 *   - id -g           -> effective gid as number
 *   - id -un / -gn    -> name forms
 *   - whoami          -> effective user name
 *   - groups          -> name of primary group only (LiteNix has no /etc/group multi-group yet)
 */

#include "libc_lite.h"

static int group_name_for(uint32_t gid, char *out, size_t out_size)
{
    int fd = open("/etc/group", O_RDONLY);
    if (fd < 0) { snprintf(out, out_size, "%u", gid); return -1; }
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) { snprintf(out, out_size, "%u", gid); return -1; }
    buf[n] = 0;
    char *line = buf;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (line[0] && line[0] != '#') {
            char tmp[256];
            strncpy(tmp, line, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = 0;
            char *p = tmp;
            char *gname = strsep_inplace(&p, ':');
            (void)strsep_inplace(&p, ':');
            char *g = strsep_inplace(&p, ':');
            if (gname && g && (uint32_t)atoi(g) == gid) {
                strncpy(out, gname, out_size - 1);
                out[out_size - 1] = 0;
                return 0;
            }
        }
        if (!nl) break;
        line = nl + 1;
    }
    snprintf(out, out_size, "%u", gid);
    return -1;
}

static int cmd_whoami(void)
{
    uint32_t euid = geteuid();
    struct passwd_ent pw;
    if (pwent_by_uid(euid, &pw) == 0) printf("%s\n", pw.name);
    else printf("%u\n", euid);
    return 0;
}

static int cmd_groups(void)
{
    uint32_t egid = getegid();
    char gname[64];
    group_name_for(egid, gname, sizeof(gname));
    printf("%s\n", gname);
    return 0;
}

static int cmd_id(int argc, char **argv)
{
    int show_uid = 0, show_gid = 0, name_only = 0, real = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0) show_uid = 1;
        else if (strcmp(argv[i], "-g") == 0) show_gid = 1;
        else if (strcmp(argv[i], "-n") == 0) name_only = 1;
        else if (strcmp(argv[i], "-r") == 0) real = 1;
        else if (strcmp(argv[i], "-un") == 0) { show_uid = 1; name_only = 1; }
        else if (strcmp(argv[i], "-gn") == 0) { show_gid = 1; name_only = 1; }
    }
    uint32_t uid = real ? getuid()  : geteuid();
    uint32_t gid = real ? getgid()  : getegid();
    char uname[64] = {0}, gname[64] = {0};
    struct passwd_ent pw;
    if (pwent_by_uid(uid, &pw) == 0) strncpy(uname, pw.name, sizeof(uname) - 1);
    else snprintf(uname, sizeof(uname), "%u", uid);
    group_name_for(gid, gname, sizeof(gname));

    if (show_uid) {
        if (name_only) printf("%s\n", uname);
        else printf("%u\n", uid);
        return 0;
    }
    if (show_gid) {
        if (name_only) printf("%s\n", gname);
        else printf("%u\n", gid);
        return 0;
    }
    printf("uid=%u(%s) gid=%u(%s) groups=%u(%s)\n", uid, uname, gid, gname, gid, gname);
    return 0;
}

int main(int argc, char **argv)
{
    const char *prog = argv[0] ? argv[0] : "id";
    /* basename */
    const char *base = prog;
    for (const char *s = prog; *s; s++) if (*s == '/' || *s == '\\') base = s + 1;

    if (strcmp(base, "whoami") == 0) return cmd_whoami();
    if (strcmp(base, "groups") == 0) return cmd_groups();
    return cmd_id(argc, argv);
}
