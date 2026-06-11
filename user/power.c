#include "libc_lite.h"
#include <stdbool.h>

int main(int argc, char **argv)
{
    (void)argc;
    bool is_reboot = false;
    
    if (argv[0] != 0) {
        // Find basename of argv[0]
        const char *base = argv[0];
        const char *p = argv[0];
        while (*p != '\0') {
            if (*p == '/') {
                base = p + 1;
            }
            p++;
        }
        if (strcmp(base, "reboot") == 0) {
            is_reboot = true;
        }
    }

    if (is_reboot) {
        printf("Requesting system reboot...\n");
        reboot(LINUX_REBOOT_CMD_RESTART);
    } else {
        printf("Requesting system poweroff...\n");
        reboot(LINUX_REBOOT_CMD_POWER_OFF);
    }

    return 0;
}
