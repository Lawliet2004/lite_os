#ifndef LITENIX_DRIVERS_TTY_H
#define LITENIX_DRIVERS_TTY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sched/wait_queue.h>

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

struct termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[32];
};

#define TTY_BUF_SIZE 4096

struct tty_device {
    char ibuf[TTY_BUF_SIZE];
    uint32_t head;
    uint32_t tail;
    struct wait_queue read_wq;
    struct winsize winsize;
    struct termios termios;
    uint32_t fg_pgid;
    uint32_t sid;
};

extern struct tty_device console_tty;

void tty_init(void);
int tty_driver_read(void *buf, size_t count);
int tty_driver_write(const void *buf, size_t count);
void tty_input_char(char ch);
int tty_ioctl(uint64_t req, void *argp);
bool tty_has_input(void);

#endif
