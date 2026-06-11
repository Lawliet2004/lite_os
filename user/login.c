#include "libc_lite.h"

static void set_env(const char *name, const char *val)
{
    (void)name;
    (void)val;
    // libc-lite doesn't have setenv, but we can pass it to execve
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    (void)set_env;

    char username[64];
    char password[64];

    for (;;) {
        printf("\nLiteNix 0.1\n");
        printf("litenix login: ");
        
        memset(username, 0, sizeof(username));
        ssize_t n = read(0, username, sizeof(username) - 1);
        if (n <= 0) continue;
        if (username[n-1] == '\n') username[n-1] = '\0';

        // In a real login, we would prompt for password without echoing
        // For v1, we just accept 'root' with any or no password.
        printf("Password: ");
        // We don't have a way to disable echo in libc-lite easily yet
        // so we just read it.
        memset(password, 0, sizeof(password));
        read(0, password, sizeof(password) - 1);

        if (strcmp(username, "root") == 0) {
            printf("\nWelcome to LiteNix, root!\n");
            
            char *sh_argv[] = { "/bin/sh", NULL };
            char *sh_envp[] = {
                "USER=root",
                "HOME=/home/root",
                "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
                "TERM=linux",
                NULL
            };
            
            chdir("/home/root");
            execve("/bin/sh", sh_argv, sh_envp);
            printf("login: execve failed\n");
            exit(1);
        } else {
            printf("\nLogin incorrect\n");
        }
    }
    return 0;
}
