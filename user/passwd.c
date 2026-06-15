/*
 * /bin/passwd -- LiteNix password management.
 *
 * Usage:
 *   passwd                    -- change current user's password
 *   passwd <user>             -- change another user's password (root only)
 *   passwd -l <user>          -- lock an account (root only)
 *   passwd -u <user>          -- unlock an account (root only)
 *
 * The new password is hashed with the libc-lite $5$ SHA-256 scheme and the
 * shadow file is rewritten atomically (write-to-temp then rename).
 */

#include "libc_lite.h"

static int prompt_password(const char *prompt, char *out, size_t out_size)
{
    printf("%s", prompt);
    struct termios saved;
    int echo_disabled = (term_echo_off(0, &saved) == 0);
    ssize_t n = read_line(0, out, out_size);
    if (echo_disabled) term_echo_restore(0, &saved);
    printf("\n");
    return (int)n;
}

static int do_lock(const char *user, int lock)
{
    if (geteuid() != 0) {
        printf("passwd: only root can lock/unlock accounts\n");
        return 1;
    }
    struct shadow_ent sh;
    if (shent_by_name(user, &sh) != 0) {
        printf("passwd: unknown user '%s'\n", user);
        return 1;
    }
    char newhash[260];
    if (lock) {
        if (sh.hash[0] == '!') { printf("passwd: account '%s' already locked\n", user); return 0; }
        snprintf(newhash, sizeof(newhash), "!%s", sh.hash);
    } else {
        if (sh.hash[0] != '!') { printf("passwd: account '%s' is not locked\n", user); return 0; }
        strncpy(newhash, sh.hash + 1, sizeof(newhash) - 1);
        newhash[sizeof(newhash) - 1] = 0;
    }
    if (shent_set_hash(user, newhash) != 0) {
        printf("passwd: failed to update /etc/shadow\n");
        return 1;
    }
    printf("passwd: %sed account '%s'\n", lock ? "lock" : "unlock", user);
    return 0;
}

int main(int argc, char **argv)
{
    /* Parse args */
    int do_lock_flag = 0, do_unlock_flag = 0;
    const char *target = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) do_lock_flag = 1;
        else if (strcmp(argv[i], "-u") == 0) do_unlock_flag = 1;
        else if (argv[i][0] != '-') target = argv[i];
    }

    /* Discover invoking user */
    uint32_t uid = getuid();
    struct passwd_ent self;
    int have_self = (pwent_by_uid(uid, &self) == 0);

    if (do_lock_flag || do_unlock_flag) {
        if (!target) { printf("Usage: passwd -l|-u <user>\n"); return 1; }
        return do_lock(target, do_lock_flag);
    }

    if (!target) {
        if (!have_self) { printf("passwd: cannot find current user\n"); return 1; }
        target = self.name;
    }

    if (uid != 0 && have_self && strcmp(target, self.name) != 0) {
        printf("passwd: only root can change another user's password\n");
        return 1;
    }

    struct passwd_ent pw;
    if (pwent_by_name(target, &pw) != 0) {
        printf("passwd: unknown user '%s'\n", target);
        return 1;
    }

    printf("passwd: Changing password for %s\n", target);

    /* If not root, require old password */
    if (uid != 0) {
        struct shadow_ent sh;
        if (shent_by_name(target, &sh) == 0 && sh.hash[0] != 0) {
            char current[128];
            if (prompt_password("Current password: ", current, sizeof(current)) < 0) return 1;
            int ok = pw_verify(current, sh.hash);
            memset(current, 0, sizeof(current));
            if (!ok) { printf("passwd: Authentication token manipulation error\n"); return 1; }
        }
    }

    char p1[128], p2[128];
    if (prompt_password("New password: ", p1, sizeof(p1)) < 0) return 1;
    if (strlen(p1) == 0) { printf("passwd: password unchanged\n"); return 1; }
    if (strlen(p1) < 4) { printf("passwd: password too short (min 4 chars)\n"); memset(p1, 0, sizeof(p1)); return 1; }
    if (prompt_password("Retype new password: ", p2, sizeof(p2)) < 0) return 1;
    if (strcmp(p1, p2) != 0) {
        printf("passwd: passwords do not match\n");
        memset(p1, 0, sizeof(p1));
        memset(p2, 0, sizeof(p2));
        return 1;
    }

    char salt[10];
    gen_salt(salt, sizeof(salt));
    char newhash[160];
    pw_hash(p1, salt, newhash, sizeof(newhash));
    memset(p1, 0, sizeof(p1));
    memset(p2, 0, sizeof(p2));

    if (shent_set_hash(target, newhash) != 0) {
        printf("passwd: failed to update /etc/shadow\n");
        return 1;
    }

    printf("passwd: password updated successfully\n");
    return 0;
}
