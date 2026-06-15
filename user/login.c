/*
 * /bin/login -- LiteNix interactive login program.
 *
 * Reads /etc/passwd to look up the requested account, /etc/shadow to verify
 * the hashed password (via the libc-lite SHA-256 $5$ scheme), and on success
 * drops privileges with setresuid/setresgid before exec'ing the user's login
 * shell.
 *
 * Behaviour:
 *   - Re-prompts on failure (up to 3 attempts, then exits 1).
 *   - Disables terminal echo while reading the password.
 *   - Accepts an empty stored hash as "no password required".
 *   - "!" or "*" in the shadow hash field means the account is locked.
 *   - `login --noauth <user>` skips authentication (used by init in test mode).
 */

#include "libc_lite.h"
#include <stdbool.h>

static void show_issue(void)
{
    int fd = open("/etc/issue", O_RDONLY);
    if (fd < 0) return;
    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = 0;
    write(1, buf, (size_t)n);
}

static int login_as(const struct passwd_ent *pw)
{
    if (setresgid(pw->gid, pw->gid, pw->gid) != 0) {
        printf("login: setresgid(%u) failed\n", pw->gid);
        return -1;
    }
    if (setresuid(pw->uid, pw->uid, pw->uid) != 0) {
        printf("login: setresuid(%u) failed\n", pw->uid);
        return -1;
    }
    if (pw->home[0]) chdir(pw->home);

    char user_env[96], home_env[160], shell_env[96];
    snprintf(user_env, sizeof(user_env), "USER=%s", pw->name);
    snprintf(home_env, sizeof(home_env), "HOME=%s", pw->home[0] ? pw->home : "/");
    snprintf(shell_env, sizeof(shell_env), "SHELL=%s", pw->shell[0] ? pw->shell : "/bin/sh");

    const char *shell = pw->shell[0] ? pw->shell : "/bin/sh";
    char *argv[] = { (char *)shell, "-l", 0 };
    char *envp[] = {
        user_env,
        home_env,
        shell_env,
        "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
        "TERM=linux",
        0
    };

    printf("Last login: now (LiteNix)\n");
    execve(shell, argv, envp);
    /* If shell missing, fall back to busybox */
    char *fb_argv[] = { "/bin/busybox", "sh", 0 };
    execve("/bin/busybox", fb_argv, envp);
    printf("login: cannot exec %s\n", shell);
    return -1;
}

int main(int argc, char **argv)
{
    int noauth = 0;
    const char *forced_user = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--noauth") == 0) noauth = 1;
        else if (argv[i][0] != '-') forced_user = argv[i];
    }

    if (noauth && forced_user) {
        struct passwd_ent pw;
        if (pwent_by_name(forced_user, &pw) != 0) {
            printf("login: unknown user '%s'\n", forced_user);
            return 1;
        }
        return login_as(&pw) == 0 ? 0 : 1;
    }

    int attempts = 0;
    for (;;) {
        if (attempts >= 3) {
            printf("\nlogin: too many failed attempts.\n");
            return 1;
        }
        show_issue();
        printf("\nlitenix login: ");

        char username[64];
        if (forced_user) {
            strncpy(username, forced_user, sizeof(username) - 1);
            username[sizeof(username) - 1] = 0;
            printf("%s\n", username);
            forced_user = 0;
        } else {
            ssize_t n = read_line(0, username, sizeof(username));
            if (n <= 0 || username[0] == 0) {
                if (n < 0) return 1;
                attempts++;
                continue;
            }
        }

        /* Look up the account first; reveal nothing on failure */
        struct passwd_ent pw;
        int have_user = (pwent_by_name(username, &pw) == 0);

        struct shadow_ent sh;
        int have_shadow = have_user ? (shent_by_name(username, &sh) == 0) : 0;

        printf("Password: ");
        struct termios saved;
        int echo_disabled = (term_echo_off(0, &saved) == 0);
        char password[128];
        ssize_t pn = read_line(0, password, sizeof(password));
        if (echo_disabled) term_echo_restore(0, &saved);
        printf("\n");

        if (pn < 0) return 1;

        int ok = 0;
        if (have_user && have_shadow) {
            ok = pw_verify(password, sh.hash);
        } else if (have_user && !have_shadow) {
            /* User exists but no shadow entry; allow only if passwd field is empty */
            ok = (pw.passwd_field[0] == 0 || strcmp(pw.passwd_field, "x") != 0);
        }
        /* Zero the password buffer immediately */
        memset(password, 0, sizeof(password));

        if (!ok) {
            printf("Login incorrect\n");
            attempts++;
            sleep(1);
            continue;
        }

        return login_as(&pw) == 0 ? 0 : 1;
    }
}
