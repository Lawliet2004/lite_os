#include <drivers/tty.h>
#include <drivers/serial.h>
#include <mm/uaccess.h>
#include <sched/scheduler.h>
#include <sched/task.h>
#include <lib/string.h>
#include <sys/syscall.h>
#include <sched/wait_queue.h>

struct tty_device console_tty;
static uint32_t commit_count = 0;
static uint32_t canon_len = 0;

void tty_init(void)
{
    memset(&console_tty, 0, sizeof(console_tty));
    wait_queue_init(&console_tty.read_wq);
    spinlock_init(&console_tty.lock, "tty");

    console_tty.winsize.ws_row = 24;
    console_tty.winsize.ws_col = 80;

    /* Sane default cooked termios */
    console_tty.termios.c_iflag = 0x500;   /* ICRNL | IXON */
    console_tty.termios.c_oflag = 0x5;     /* OPOST | ONLCR */
    console_tty.termios.c_cflag = 0xBF;    /* CS8 | CREAD | HUPCL */
    console_tty.termios.c_lflag = 0x8A3B;  /* ISIG | ICANON | ECHO | ECHOE | ECHOK */

    /* Control characters: VINTR = c_cc[0], VEOF = c_cc[4], VMIN = c_cc[6] */
    console_tty.termios.c_cc[0] = 3;  /* Ctrl-C */
    console_tty.termios.c_cc[4] = 4;  /* Ctrl-D */
    console_tty.termios.c_cc[6] = 1;  /* VMIN */

    commit_count = 0;
    canon_len = 0;
}

int tty_driver_write(const void *buf, size_t count)
{
    const char *cbuf = buf;
    for (size_t i = 0; i < count; i++) {
        serial_write_char(cbuf[i]);
    }
    return (int)count;
}

void tty_input_char(char ch)
{
    uint64_t flags;
    spin_lock_irqsave(&console_tty.lock, &flags);
    struct termios *t = &console_tty.termios;

    /* 1. Job Control / Interrupt signal handling */
    if ((t->c_lflag & 0x0001) /* ISIG */ && ch == t->c_cc[0] /* Ctrl-C */) {
        if (t->c_lflag & 0x0008) /* ECHO */ {
            serial_write_char('^');
            serial_write_char('C');
            serial_write_char('\n');
        }
        if (console_tty.fg_pgid != 0) {
            task_send_signal_pgid(console_tty.fg_pgid, 2); /* SIGINT */
        } else if (current_task != 0 && current_task->process != 0) {
            task_send_signal(current_task, 2); /* SIGINT fallback */
        }
        spin_unlock_irqrestore(&console_tty.lock, flags);
        return;
    }

    /* Ctrl-Z: Job control suspend signal */
    if ((t->c_lflag & 0x0001) /* ISIG */ && ch == 26 /* Ctrl-Z */) {
        if (t->c_lflag & 0x0008) /* ECHO */ {
            serial_write_char('^');
            serial_write_char('Z');
            serial_write_char('\n');
        }
        if (console_tty.fg_pgid != 0) {
            task_send_signal_pgid(console_tty.fg_pgid, 20); /* SIGTSTP */
        } else if (current_task != 0 && current_task->process != 0) {
            task_send_signal(current_task, 20); /* SIGTSTP fallback */
        }
        spin_unlock_irqrestore(&console_tty.lock, flags);
        return;
    }

    /* 2. Canonical mode line discipline */
    if (t->c_lflag & 0x0002) /* ICANON */ {
        if (ch == 127 || ch == 8) /* Backspace */ {
            if (canon_len > 0) {
                /* Remove character from ring buffer */
                if (console_tty.head > 0) {
                    console_tty.head--;
                } else {
                    console_tty.head = TTY_BUF_SIZE - 1;
                }
                canon_len--;
                if (t->c_lflag & 0x0008) /* ECHO */ {
                    serial_write_char('\b');
                    serial_write_char(' ');
                    serial_write_char('\b');
                }
            }
            return;
        }

        if (ch == t->c_cc[4] /* Ctrl-D (EOF) */) {
            canon_len = 0;
            commit_count++;
            wait_queue_wake_all(&console_tty.read_wq);
            io_event_notify();
            return;
        }

        /* Carriage Return to Newline translation */
        if (ch == '\r' && (t->c_iflag & 0x0100) /* ICRNL */) {
            ch = '\n';
        }

        /* Echo character if enabled */
        if (t->c_lflag & 0x0008) /* ECHO */ {
            serial_write_char(ch);
        }

        /* Append to ring buffer */
        uint32_t next_head = (console_tty.head + 1) % TTY_BUF_SIZE;
        if (next_head != console_tty.tail) {
            console_tty.ibuf[console_tty.head] = ch;
            console_tty.head = next_head;
                if (ch == '\n') {
                    canon_len = 0;
                    commit_count++;
                    wait_queue_wake_all(&console_tty.read_wq);
                    io_event_notify();
                } else {
                    canon_len++;
                }
        }
    } else {
        /* Raw / Non-canonical Mode */
        if (t->c_lflag & 0x0008) /* ECHO */ {
            serial_write_char(ch);
        }

        uint32_t next_head = (console_tty.head + 1) % TTY_BUF_SIZE;
        if (next_head != console_tty.tail) {
            console_tty.ibuf[console_tty.head] = ch;
            console_tty.head = next_head;
            wait_queue_wake_all(&console_tty.read_wq);
            io_event_notify();
        }
    }
    spin_unlock_irqrestore(&console_tty.lock, flags);
}

int tty_driver_read(void *buf, size_t count)
{
    if (count == 0) return 0;
    uint64_t flags;
    spin_lock_irqsave(&console_tty.lock, &flags);
    struct termios *t = &console_tty.termios;
    char *cbuf = buf;
    size_t read_bytes = 0;

    if (t->c_lflag & 0x0002) /* ICANON */ {
        /* Block until at least one committed line (ending with \n or EOF) is ready */
        while (commit_count == 0) {
            wait_queue_sleep_locked(&console_tty.read_wq);
        }

        /* Copy from ring buffer until \n is consumed or buffer is empty */
        while (read_bytes < count && console_tty.head != console_tty.tail) {
            char ch = console_tty.ibuf[console_tty.tail];
            console_tty.tail = (console_tty.tail + 1) % TTY_BUF_SIZE;
            cbuf[read_bytes++] = ch;
            if (ch == '\n') {
                if (commit_count > 0) commit_count--;
                break;
            }
        }

        /* If we reached tail == head but haven't seen a newline, we must have read an EOF commit */
        if (read_bytes == 0 && commit_count > 0) {
            commit_count--;
        }
    } else {
        /* Non-canonical mode */
        uint32_t min_chars = t->c_cc[6] > 0 ? t->c_cc[6] : 1;
        while (((console_tty.head - console_tty.tail + TTY_BUF_SIZE) % TTY_BUF_SIZE) < min_chars) {
            wait_queue_sleep_locked(&console_tty.read_wq);
        }

        while (read_bytes < count && console_tty.head != console_tty.tail) {
            char ch = console_tty.ibuf[console_tty.tail];
            console_tty.tail = (console_tty.tail + 1) % TTY_BUF_SIZE;
            cbuf[read_bytes++] = ch;
        }
    }
    spin_unlock_irqrestore(&console_tty.lock, flags);

    return (int)read_bytes;
}

bool tty_has_input(void)
{
    uint64_t flags;
    spin_lock_irqsave(&console_tty.lock, &flags);
    bool r;
    if ((console_tty.termios.c_lflag & 0x0002) /* ICANON */) {
        r = (commit_count != 0);
    } else {
        r = (console_tty.head != console_tty.tail);
    }
    spin_unlock_irqrestore(&console_tty.lock, flags);
    return r;
}

int tty_ioctl(uint64_t req, void *argp)
{
    switch (req) {
    case 0x5413: /* TIOCGWINSZ */ {
        if (copy_to_user(argp, &console_tty.winsize, sizeof(struct winsize)) != 0) {
            return -EFAULT;
        }
        return 0;
    }
    case 0x5414: /* TIOCSWINSZ */ {
        if (copy_from_user(&console_tty.winsize, argp, sizeof(struct winsize)) != 0) {
            return -EFAULT;
        }
        return 0;
    }
    case 0x5401: /* TCGETS */ {
        if (copy_to_user(argp, &console_tty.termios, sizeof(struct termios)) != 0) {
            return -EFAULT;
        }
        return 0;
    }
    case 0x5402: /* TCSETS */
    case 0x5403: /* TCSETSW */
    case 0x5404: /* TCSETSF */ {
        if (copy_from_user(&console_tty.termios, argp, sizeof(struct termios)) != 0) {
            return -EFAULT;
        }
        return 0;
    }
    case 0x540F: /* TIOCGPGRP */ {
        uint32_t pgid = console_tty.fg_pgid;
        if (copy_to_user(argp, &pgid, sizeof(uint32_t)) != 0) {
            return -EFAULT;
        }
        return 0;
    }
    case 0x5410: /* TIOCSPGRP */ {
        uint32_t pgid;
        if (copy_from_user(&pgid, argp, sizeof(uint32_t)) != 0) {
            return -EFAULT;
        }
        console_tty.fg_pgid = pgid;
        return 0;
    }
    default:
        return -EINVAL;
    }
}
