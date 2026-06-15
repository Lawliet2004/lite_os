#include "libc_lite.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("HELLO FROM MINIMAL INIT\n");
    printf("FD 0, 1, 2: %d %d %d\n", 0, 1, 2);
    printf("getpid()=%d getuid()=%d\n", getpid(), getuid());
    return 0;
}
