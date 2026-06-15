/*
 * /bin/su -- LiteNix substitute user identity.
 *
 * Usage:
 *   su            -- become root (asks for root password unless invoker is root)
 *   su <user>     -- become another user
 *   su -l <user>  -- start a login shell (sets HOME/PWD)
 *   su -c <cmd>   -- run a single command then exit
 *
 * Behaviour:
 *   - Root may switch to any user without a password.
 *   - Other users must supply the target user's password.
 *   - Forks a child that executes the target shell with the target's UID/GID.
 */

#include "libc_lite.h"

int main(int argc, char **argv)
{
    int login_shell = 0;
    const char *target = "root";
    const char *cmd = 0;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--login") == 0) {
            login_shell = 1;
            i++;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cmd = argv[i + 1];
            i += 2;
        } else if (argv[i][0] == '-') {
            i++;
        } else {
            target = argv[i++];
        }
    }

    struct passwd_ent pw;
    if (pwent_by_name(target, &pw) != 0) {
        printf("su: unknown user '%s'\n", target);
        return 1;
    }

    uint32_t my_uid = getuid();
    if (my_uid != 0) {
        struct shadow_ent sh;
        if (shent_by_name(target, &sh) != 0) {
            printf("su: cannot read /etc/shadow\n");
            return 1;
        }
        if (sh.hash[0] == '!' || sh.hash[0] == '*') {
            printf("su: account '%s' is locked\n", target);
            return 1;
        }
        if (sh.hash[0] != 0) {
            printf("Password: ");
            struct termios saved;
            int echo_disabled = (term_echo_off(0, &saved) == 0);
            char password[128];
            ssize_t n = read_line(0, password, sizeof(password));
            if (echo_disabled) term_echo_restore(0, &saved);
            printf("\n");
            if (n < 0) return 1;
            int ok = pw_verify(password, sh.hash);
            memset(password, 0, sizeof(password));
            if (!ok) { printf("su: Authentication failure\n"); sleep(1); return 1; }
        }
    }

    if (setresgid(pw.gid, pw.gid, pw.gid) != 0) { printf("su: setresgid failed\n"); return 1; }
    if (setresuid(pw.uid, pw.uid, pw.uid) != 0) { printf("su: setresuid failed\n"); return 1; }

    if (login_shell && pw.home[0]) chdir(pw.home);

    char user_env[96], home_env[160], shell_env[96];
    snprintf(user_env, sizeof(user_env), "USER=%s", pw.name);
    snprintf(home_env, sizeof(home_env), "HOME=%s", pw.home[0] ? pw.home : "/");
    snprintf(shell_env, sizeof(shell_env), "SHELL=%s", pw.shell[0] ? pw.shell : "/bin/sh");
    char *envp[] = {
        user_env, home_env, shell_env,
        "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
        "TERM=linux",
        0
    };

    const char *shell = pw.shell[0] ? pw.shell : "/bin/sh";
    if (cmd) {
        char *sh_argv[] = { (char *)shell, "-c", (char *)cmd, 0 };
        execve(shell, sh_argv, envp);
        char *fb[] = { "/bin/busybox", "sh", "-c", (char *)cmd, 0 };
        execve("/bin/busybox", fb, envp);
    } else {
        char *sh_argv[] = { (char *)shell, login_shell ? "-l" : 0, 0 };
        execve(shell, sh_argv, envp);
        char *fb[] = { "/bin/busybox", "sh", 0 };
        execve("/bin/busybox", fb, envp);
    }
    printf("su: cannot exec %s\n", shell);
    return 1;
}
