/*
 * /tests/show_creds — exits with status set to the current effective UID.
 *
 * Used by init's Test 33 to verify that exec'ing a setuid-root binary from
 * a non-root parent causes the kernel to set euid=0 on the new process.
 *
 * Exit codes are capped to 0..255 (the lower 8 bits Linux makes available
 * via WEXITSTATUS), which is enough to distinguish UID 0 from UID 1000.
 */

#include "libc_lite.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t euid = geteuid();
    uint32_t uid  = getuid();
    /* Print to serial so the verify-boot log shows what happened */
    printf("show_creds: ruid=%u euid=%u\n", uid, euid);
    /* Stuff the euid into the exit code so the parent can wait4-check it */
    return (int)(euid & 0xff);
}
