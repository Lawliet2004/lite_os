#include "libc_lite.h"
#include <stdbool.h>

#define DB_DIR "/var/lib/lpkg/installed"
#define REPO_DIR "/usr/share/packages"

static uint8_t file_data_buf[256 * 1024]; // 256 KB static buffer for package extraction

static uint32_t calculate_checksum(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

static bool contains_path_traversal(const char *path)
{
    if (path[0] == '\0') return false;
    const char *p = path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.') {
            if (p == path || p[-1] == '/' || p[-1] == '\\') {
                if (p[2] == '\0' || p[2] == '/' || p[2] == '\\') {
                    return true;
                }
            }
        }
        p++;
    }
    if (path[0] == '/' || path[0] == '\\') {
        if (path[1] == '.' && path[2] == '.') {
            return true;
        }
    }
    return false;
}

static void make_parents(const char *filepath)
{
    char path[256];
    strncpy(path, filepath, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    for (int i = 1; path[i] != '\0'; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            char save = path[i];
            path[i] = '\0';
            if (path[0] != '\0') mkdir(path, 0755);
            path[i] = save;
        }
    }
}

static int do_install(const char *lpkg_path)
{
    int fd = open(lpkg_path, O_RDONLY);
    if (fd < 0) {
        // Try in repo dir if not found
        char repo_path[512];
        snprintf(repo_path, sizeof(repo_path), "%s/%s.lpkg", REPO_DIR, lpkg_path);
        fd = open(repo_path, O_RDONLY);
        if (fd < 0) {
             printf("lpkg: failed to open package file '%s'\n", lpkg_path);
             return 1;
        }
    }

    char magic[4];
    if (read(fd, magic, 4) != 4 || memcmp(magic, "LPKG", 4) != 0) {
        printf("lpkg: invalid package magic\n");
        close(fd);
        return 1;
    }

    uint32_t file_count = 0;
    if (read(fd, &file_count, 4) != 4) {
        printf("lpkg: failed to read file count\n");
        close(fd);
        return 1;
    }

    mkdir("/var", 0755);
    mkdir("/var/lib", 0755);
    mkdir("/var/lib/lpkg", 0755);
    mkdir(DB_DIR, 0755);

    char pkg_name[64] = {0};
    char pkg_version[32] = {0};
    char pkg_desc[128] = {0};
    char pkg_info_buf[4096] = {0};
    char files_list_buf[8192] = {0};
    size_t files_list_len = 0;

    for (uint32_t i = 0; i < file_count; i++) {
        uint32_t path_len = 0;
        if (read(fd, &path_len, 4) != 4 || path_len > 255) {
            printf("lpkg: invalid path length\n");
            close(fd);
            return 1;
        }

        char rel_path[256];
        if (read(fd, rel_path, path_len) != (ssize_t)path_len) {
            printf("lpkg: failed to read path\n");
            close(fd);
            return 1;
        }
        rel_path[path_len] = '\0';

        uint32_t mode = 0;
        uint32_t size = 0;
        uint32_t checksum = 0;
        if (read(fd, &mode, 4) != 4 || read(fd, &size, 4) != 4 || read(fd, &checksum, 4) != 4) {
            printf("lpkg: failed to read file metadata\n");
            close(fd);
            return 1;
        }

        if (contains_path_traversal(rel_path)) {
            printf("lpkg: security error: path traversal detected in '%s'\n", rel_path);
            close(fd);
            return 1;
        }

        if (size >= sizeof(file_data_buf)) {
            printf("lpkg: error: file '%s' is too large for buffer (%u bytes)\n", rel_path, size);
            close(fd);
            return 1;
        }

        uint8_t *file_data = file_data_buf;
        if (size > 0 && read(fd, file_data, size) != (ssize_t)size) {
            printf("lpkg: failed to read file content for '%s'\n", rel_path);
            close(fd);
            return 1;
        }

        uint32_t computed = calculate_checksum(file_data, size);
        if (computed != checksum) {
            printf("lpkg: integrity check failed for '%s' (expected %u, got %u)\n", rel_path, checksum, computed);
            close(fd);
            return 1;
        }

        if (strcmp(rel_path, "PKGINFO") == 0 || strcmp(rel_path, ".PKGINFO") == 0) {
            size_t copy_size = size < sizeof(pkg_info_buf) - 1 ? size : sizeof(pkg_info_buf) - 1;
            memcpy(pkg_info_buf, file_data, copy_size);
            pkg_info_buf[copy_size] = '\0';

            char *line = pkg_info_buf;
            while (*line) {
                char *next = strchr(line, '\n');
                if (next) *next = '\0';

                if (strncmp(line, "name=", 5) == 0) {
                    strncpy(pkg_name, line + 5, sizeof(pkg_name) - 1);
                } else if (strncmp(line, "version=", 8) == 0) {
                    strncpy(pkg_version, line + 8, sizeof(pkg_version) - 1);
                } else if (strncmp(line, "desc=", 5) == 0) {
                    strncpy(pkg_desc, line + 5, sizeof(pkg_desc) - 1);
                } else if (strncmp(line, "description=", 12) == 0) {
                    strncpy(pkg_desc, line + 12, sizeof(pkg_desc) - 1);
                }

                if (next) {
                    line = next + 1;
                } else {
                    break;
                }
            }
            continue;
        }

        char dest_path[512];
        if (rel_path[0] == '/') {
            snprintf(dest_path, sizeof(dest_path), "%s", rel_path);
        } else {
            snprintf(dest_path, sizeof(dest_path), "/%s", rel_path);
        }

        printf("  extracting: %s\n", dest_path);
        make_parents(dest_path);

        int out_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC);
        if (out_fd < 0) {
            printf("lpkg: failed to write file '%s'\n", dest_path);
            close(fd);
            return 1;
        }
        if (size > 0) {
            write(out_fd, file_data, size);
        }
        close(out_fd);

        size_t entry_len = strlen(dest_path);
        if (files_list_len + entry_len + 2 < sizeof(files_list_buf)) {
            files_list_len += snprintf(files_list_buf + files_list_len, sizeof(files_list_buf) - files_list_len, "%s\n", dest_path);
        }
    }

    close(fd);

    if (pkg_name[0] == '\0') {
        printf("lpkg: error: package is missing valid name in PKGINFO\n");
        return 1;
    }

    printf("Installing package %s (%s)...\n", pkg_name, pkg_version);

    char meta_path[256];
    snprintf(meta_path, sizeof(meta_path), "%s/%s.meta", DB_DIR, pkg_name);
    int meta_fd = open(meta_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (meta_fd >= 0) {
        if (pkg_info_buf[0] != '\0') {
            write(meta_fd, pkg_info_buf, strlen(pkg_info_buf));
        } else {
            char basic[256];
            snprintf(basic, sizeof(basic), "name=%s\nversion=%s\ndesc=%s\n", pkg_name, pkg_version, pkg_desc);
            write(meta_fd, basic, strlen(basic));
        }
        close(meta_fd);
    }

    char list_path[256];
    snprintf(list_path, sizeof(list_path), "%s/%s.list", DB_DIR, pkg_name);
    int list_fd = open(list_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (list_fd >= 0) {
        write(list_fd, files_list_buf, strlen(files_list_buf));
        close(list_fd);
    }

    printf("Successfully installed %s-%s\n", pkg_name, pkg_version);
    return 0;
}

static int do_installed(void)
{
    printf("Installed packages:\n");
    int fd = open(DB_DIR, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        printf("  None (database directory missing)\n");
        return 0;
    }

    char buf[1024];
    int nread;
    bool found = false;
    while ((nread = getdents64(fd, (struct linux_dirent64 *)buf, sizeof(buf))) > 0) {
        struct linux_dirent64 *d;
        for (int bpos = 0; bpos < nread; bpos += d->d_reclen) {
            d = (struct linux_dirent64 *)(buf + bpos);
            char *ext = strchr(d->d_name, '.');
            if (ext && strcmp(ext, ".meta") == 0) {
                found = true;
                char meta_path[512];
                snprintf(meta_path, sizeof(meta_path), "%s/%s", DB_DIR, d->d_name);
                int mfd = open(meta_path, O_RDONLY);
                if (mfd >= 0) {
                    char content[1024];
                    ssize_t r = read(mfd, content, sizeof(content) - 1);
                    if (r > 0) {
                        content[r] = '\0';
                        char name[64] = {0};
                        char ver[32] = {0};
                        char desc[128] = {0};
                        char *line = content;
                        while (*line) {
                            char *next = strchr(line, '\n');
                            if (next) *next = '\0';
                            if (strncmp(line, "name=", 5) == 0) {
                                strncpy(name, line + 5, sizeof(name) - 1);
                            } else if (strncmp(line, "version=", 8) == 0) {
                                strncpy(ver, line + 8, sizeof(ver) - 1);
                            } else if (strncmp(line, "desc=", 5) == 0) {
                                strncpy(desc, line + 5, sizeof(desc) - 1);
                            } else if (strncmp(line, "description=", 12) == 0) {
                                strncpy(desc, line + 12, sizeof(desc) - 1);
                            }
                            if (next) line = next + 1;
                            else break;
                        }
                        printf("  %s %s - %s\n", name, ver, desc);
                    }
                    close(mfd);
                }
            }
        }
    }
    close(fd);

    if (!found) {
        printf("  None\n");
    }
    return 0;
}

static int do_list(void)
{
    printf("Available packages in repository:\n");
    int fd = open(REPO_DIR, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        printf("  Repository directory missing (%s)\n", REPO_DIR);
        return 0;
    }

    char buf[1024];
    int nread;
    bool found = false;
    while ((nread = getdents64(fd, (struct linux_dirent64 *)buf, sizeof(buf))) > 0) {
        struct linux_dirent64 *d;
        for (int bpos = 0; bpos < nread; bpos += d->d_reclen) {
            d = (struct linux_dirent64 *)(buf + bpos);
            if (d->d_name[0] == '.') continue;
            printf("  %s\n", d->d_name);
            found = true;
        }
    }
    close(fd);

    if (!found) {
        printf("  None\n");
    }
    return 0;
}

static int do_info(const char *name)
{
    char meta_path[256];
    snprintf(meta_path, sizeof(meta_path), "%s/%s.meta", DB_DIR, name);
    int fd = open(meta_path, O_RDONLY);
    if (fd < 0) {
        printf("Error: package '%s' is not installed\n", name);
        return 1;
    }
    printf("--- Package Metadata ---\n");
    char buf[1024];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
    }
    close(fd);

    char list_path[256];
    snprintf(list_path, sizeof(list_path), "%s/%s.list", DB_DIR, name);
    fd = open(list_path, O_RDONLY);
    if (fd >= 0) {
        printf("\n--- Package Files ---\n");
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            write(1, buf, n);
        }
        close(fd);
    }
    return 0;
}

static int do_remove(const char *name)
{
    char list_path[256];
    snprintf(list_path, sizeof(list_path), "%s/%s.list", DB_DIR, name);
    int fd = open(list_path, O_RDONLY);
    if (fd < 0) {
        printf("Error: package '%s' is not installed\n", name);
        return 1;
    }

    printf("Removing %s...\n", name);

    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n > 0) {
        buf[n] = '\0';
        char *line = buf;
        while (*line) {
            char *next = strchr(line, '\n');
            if (next) *next = '\0';

            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\n')) {
                line[len - 1] = '\0';
                len--;
            }

            if (len > 0) {
                printf("  deleting: %s\n", line);
                unlink(line);
            }

            if (next) line = next + 1;
            else break;
        }
    }

    unlink(list_path);

    char meta_path[256];
    snprintf(meta_path, sizeof(meta_path), "%s/%s.meta", DB_DIR, name);
    unlink(meta_path);

    printf("Successfully removed %s\n", name);
    return 0;
}

static int do_verify(const char *name)
{
    char list_path[256];
    snprintf(list_path, sizeof(list_path), "%s/%s.list", DB_DIR, name);
    int fd = open(list_path, O_RDONLY);
    if (fd < 0) {
        printf("Error: package '%s' is not installed\n", name);
        return 1;
    }

    printf("Verifying %s...\n", name);

    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    int missing = 0;
    if (n > 0) {
        buf[n] = '\0';
        char *line = buf;
        while (*line) {
            char *next = strchr(line, '\n');
            if (next) *next = '\0';

            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\n')) {
                line[len - 1] = '\0';
                len--;
            }

            if (len > 0) {
                struct stat st;
                if (stat(line, &st) != 0) {
                    printf("  MISSING: %s\n", line);
                    missing++;
                }
            }

            if (next) line = next + 1;
            else break;
        }
    }

    if (missing == 0) {
        printf("Package %s verified successfully.\n", name);
        return 0;
    } else {
        printf("Package %s verification FAILED (%d files missing).\n", name, missing);
        return 1;
    }
}

static void do_help(void)
{
    printf("LiteNix Package Manager (lpkg)\n");
    printf("Usage: lpkg <command> [args]\n\n");
    printf("Commands:\n");
    printf("  list              List available packages in repository\n");
    printf("  installed         List installed packages\n");
    printf("  install <pkg>     Install a package (.lpkg file or name in repo)\n");
    printf("  remove <name>     Remove an installed package\n");
    printf("  info <name>       Show detailed package information\n");
    printf("  verify <name>     Verify installed package files\n");
    printf("  update            Update repository (stub)\n");
    printf("  help              Show this help\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        do_help();
        return 1;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "install") == 0) {
        if (argc < 3) {
            printf("Usage: lpkg install <package.lpkg>\n");
            return 1;
        }
        return do_install(argv[2]);
    } else if (strcmp(cmd, "list") == 0) {
        return do_list();
    } else if (strcmp(cmd, "installed") == 0) {
        return do_installed();
    } else if (strcmp(cmd, "info") == 0) {
        if (argc < 3) {
            printf("Usage: lpkg info <name>\n");
            return 1;
        }
        return do_info(argv[2]);
    } else if (strcmp(cmd, "remove") == 0) {
        if (argc < 3) {
            printf("Usage: lpkg remove <name>\n");
            return 1;
        }
        return do_remove(argv[2]);
    } else if (strcmp(cmd, "verify") == 0) {
        if (argc < 3) {
            printf("Usage: lpkg verify <name>\n");
            return 1;
        }
        return do_verify(argv[2]);
    } else if (strcmp(cmd, "update") == 0) {
        printf("Repository update not implemented yet.\n");
        return 0;
    } else if (strcmp(cmd, "help") == 0) {
        do_help();
        return 0;
    } else {
        printf("Unknown command: %s. Use 'lpkg help' for usage.\n", cmd);
        return 1;
    }
}
