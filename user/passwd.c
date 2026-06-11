#include "libc_lite.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("passwd: Changing password for root.\n");
    printf("(current) UNIX password: ");
    
    char buf[64];
    read(0, buf, sizeof(buf));
    
    printf("Enter new UNIX password: ");
    read(0, buf, sizeof(buf));
    
    printf("Retype new UNIX password: ");
    read(0, buf, sizeof(buf));
    
    printf("passwd: password not implemented yet in v1. Changes NOT saved.\n");
    return 1;
}
