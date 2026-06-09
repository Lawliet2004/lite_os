#include <unistd.h>
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const char msg[] = "Hello from dynamic musl!\n";
    write(1, msg, sizeof(msg)-1);
    return 0;
}