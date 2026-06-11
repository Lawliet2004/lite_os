#include "libc_lite.h"

extern int resolve_host(const char *hostname, char *out_ip, size_t out_max);

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: dnslookup <hostname>\n");
        return 1;
    }

    char resolved_ip[64];
    printf("Resolving %s...\n", argv[1]);
    if (resolve_host(argv[1], resolved_ip, sizeof(resolved_ip)) == 0) {
        printf("%s resolved to %s\n", argv[1], resolved_ip);
        return 0;
    } else {
        printf("Failed to resolve %s\n", argv[1]);
        return 1;
    }
}
