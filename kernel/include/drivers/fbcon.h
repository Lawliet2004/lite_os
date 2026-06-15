#ifndef LITENIX_DRIVERS_FBCON_H
#define LITENIX_DRIVERS_FBCON_H

void fbcon_init(void);
void fbcon_putc(char c);
void fbcon_puts(const char *s);

#endif
