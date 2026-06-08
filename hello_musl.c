#include <stdio.h>
int main(int argc, char **argv, char **envp) {
    printf("Hello from musl!\n");
    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d] = %s\n", i, argv[i]);
    }
    if (envp && envp[0]) {
        printf("envp[0] = %s\n", envp[0]);
    }
    return 0;
}
