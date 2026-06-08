#include "libc_lite.h"
#include <stdbool.h>

#define MAX_ENV_VARS 64
#define MAX_ENV_LEN 256
static char env_pool[MAX_ENV_VARS][MAX_ENV_LEN];
static char *env_vars[MAX_ENV_VARS + 1];
static int env_count = 0;

static const char *get_env(const char *name)
{
    size_t name_len = strlen(name);
    for (int i = 0; i < env_count; i++) {
        if (strncmp(env_vars[i], name, name_len) == 0 && env_vars[i][name_len] == '=') {
            return env_vars[i] + name_len + 1;
        }
    }
    return 0;
}

static void set_env(const char *name, const char *value)
{
    size_t name_len = strlen(name);
    int idx = -1;
    for (int i = 0; i < env_count; i++) {
        if (strncmp(env_vars[i], name, name_len) == 0 && env_vars[i][name_len] == '=') {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        if (env_count >= MAX_ENV_VARS) {
            printf("export: environment variable limit reached\n");
            return;
        }
        idx = env_count++;
    }

    snprintf(env_pool[idx], MAX_ENV_LEN, "%s=%s", name, value);
    env_vars[idx] = env_pool[idx];
    env_vars[env_count] = 0;
}

static void print_env(void)
{
    for (int i = 0; i < env_count; i++) {
        printf("%s\n", env_vars[i]);
    }
}

static bool find_command(const char *cmd, char *out_path, size_t max_len)
{
    if (cmd[0] == '/' || (cmd[0] == '.' && (cmd[1] == '/' || (cmd[1] == '.' && cmd[2] == '/')))) {
        struct stat st;
        if (stat(cmd, &st) == 0) {
            strncpy(out_path, cmd, max_len - 1);
            out_path[max_len - 1] = '\0';
            return true;
        }
        return false;
    }

    const char *path_env = get_env("PATH");
    if (path_env == 0) {
        path_env = "/bin";
    }

    char path_buf[256];
    strncpy(path_buf, path_env, sizeof(path_buf) - 1);
    path_buf[sizeof(path_buf) - 1] = '\0';

    char *dir = path_buf;
    while (dir != 0 && *dir != '\0') {
        char *next = strchr(dir, ':');
        if (next != 0) {
            *next = '\0';
            next++;
        }

        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            strncpy(out_path, full_path, max_len - 1);
            out_path[max_len - 1] = '\0';
            return true;
        }

        dir = next;
    }

    return false;
}

static void parse_args(char *line, char **args, int *arg_count)
{
    int count = 0;
    char *p = line;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        args[count++] = p;
        if (count >= 15) {
            break;
        }
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
    }
    args[count] = 0;
    *arg_count = count;
}

static void run_ls(const char *dir_path)
{
    int fd = open(dir_path, O_RDONLY);
    if (fd < 0) {
        printf("ls: Cannot open '%s'\n", dir_path);
        return;
    }

    char buf[512];
    int nread = getdents64(fd, (struct linux_dirent64 *)buf, sizeof(buf));
    if (nread < 0) {
        printf("ls: Failed to read directory entries\n");
        close(fd);
        return;
    }

    int bpos = 0;
    while (bpos < nread) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
        if (d->d_type == DT_DIR) {
            printf("%s/\n", d->d_name);
        } else {
            printf("%s\n", d->d_name);
        }
        bpos += d->d_reclen;
    }

    close(fd);
}

static void run_cat(const char *file_path)
{
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        printf("cat: Cannot open '%s'\n", file_path);
        return;
    }

    char buf[256];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    if (n < 0) {
        printf("\ncat: Error reading file\n");
    } else {
        printf("\n");
    }

    close(fd);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv;

    // Copy envp to env_pool
    if (envp != 0) {
        for (int i = 0; envp[i] != 0 && env_count < MAX_ENV_VARS; i++) {
            strncpy(env_pool[env_count], envp[i], MAX_ENV_LEN - 1);
            env_pool[env_count][MAX_ENV_LEN - 1] = '\0';
            env_vars[env_count] = env_pool[env_count];
            env_count++;
        }
    }
    env_vars[env_count] = 0;

    // Set default PATH if not set
    if (get_env("PATH") == 0) {
        set_env("PATH", "/bin");
    }

    printf("Welcome to LiteNix Shell!\n");
    printf("Type 'help' for available commands.\n\n");

    char line[256];
    char *args[16];
    int arg_count;

    for (;;) {
        printf("litenix$ ");

        int len = 0;
        while (len < 255) {
            char ch;
            ssize_t r = read(0, &ch, 1);
            if (r <= 0) {
                break;
            }
            if (ch == '\n' || ch == '\r') {
                break;
            }
            if (ch == '\b' || ch == 127) {
                if (len > 0) {
                    len--;
                    write(1, " \b", 2); // erase character on terminal
                }
            } else {
                line[len++] = ch;
            }
        }
        line[len] = '\0';

        parse_args(line, args, &arg_count);
        if (arg_count == 0) {
            continue;
        }

        // Expand environment variables
        for (int i = 0; i < arg_count; i++) {
            if (args[i][0] == '$') {
                const char *val = get_env(args[i] + 1);
                if (val != 0) {
                    args[i] = (char *)val;
                } else {
                    args[i] = "";
                }
            }
        }

        if (strcmp(args[0], "exit") == 0) {
            printf("Exiting shell...\n");
            exit(0);
        } else if (strcmp(args[0], "help") == 0) {
            printf("Available builtins:\n");
            printf("  cd <dir>    Change current working directory\n");
            printf("  pwd         Print current working directory\n");
            printf("  ls          List files in current directory\n");
            printf("  ls <dir>    List files in <dir>\n");
            printf("  cat <file>  Display file contents\n");
            printf("  echo <args> Display text arguments\n");
            printf("  env         Print environment variables\n");
            printf("  export NAME=VALUE  Set environment variable\n");
            printf("  exit        Exit the shell\n");
            printf("  help        Show this help message\n");
        } else if (strcmp(args[0], "cd") == 0) {
            const char *path = "/";
            if (arg_count > 1) {
                path = args[1];
            }
            if (chdir(path) != 0) {
                printf("cd: no such file or directory: %s\n", path);
            }
        } else if (strcmp(args[0], "pwd") == 0) {
            char cwd_buf[256];
            if (getcwd(cwd_buf, sizeof(cwd_buf)) != 0) {
                printf("%s\n", cwd_buf);
            } else {
                printf("pwd: error getting current directory\n");
            }
        } else if (strcmp(args[0], "ls") == 0) {
            const char *path = ".";
            if (arg_count > 1) {
                path = args[1];
            }
            run_ls(path);
        } else if (strcmp(args[0], "cat") == 0) {
            if (arg_count < 2) {
                printf("Usage: cat <filename>\n");
            } else {
                run_cat(args[1]);
            }
        } else if (strcmp(args[0], "echo") == 0) {
            for (int i = 1; i < arg_count; i++) {
                printf("%s%s", args[i], (i == arg_count - 1) ? "" : " ");
            }
            printf("\n");
        } else if (strcmp(args[0], "env") == 0) {
            print_env();
        } else if (strcmp(args[0], "export") == 0) {
            if (arg_count < 2) {
                printf("Usage: export NAME=VALUE\n");
            } else {
                char *eq = strchr(args[1], '=');
                if (eq != 0) {
                    *eq = '\0';
                    const char *name = args[1];
                    const char *val = eq + 1;
                    set_env(name, val);
                } else {
                    printf("Usage: export NAME=VALUE\n");
                }
            }
        } else {
            // External command execution
            int pid = fork();
            if (pid < 0) {
                printf("sh: fork failed\n");
            } else if (pid == 0) {
                char exec_path[256];
                if (find_command(args[0], exec_path, sizeof(exec_path))) {
                    execve(exec_path, args, env_vars);
                    printf("sh: execve failed\n");
                } else {
                    printf("sh: command not found: %s\n", args[0]);
                }
                exit(127);
            } else {
                int status = 0;
                wait4(pid, &status, 0, 0);
                if (WIFSIGNALED(status)) {
                    printf("sh: Process terminated by signal %d\n", WTERMSIG(status));
                }
            }
        }
    }
    return 0;
}
