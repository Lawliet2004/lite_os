#ifndef LITENIX_DRIVERS_SERIAL_H
#define LITENIX_DRIVERS_SERIAL_H

#include <stdbool.h>

void serial_init(void);
bool serial_is_initialized(void);
void serial_write_char(char ch);
void serial_write_string(const char *text);

#endif
