#ifndef LITENIX_DRIVERS_VGA_TEXT_H
#define LITENIX_DRIVERS_VGA_TEXT_H

void vga_text_init(void);
void vga_text_write_char(char ch);
void vga_text_write_string(const char *text);

#endif
