#ifndef LITENIX_DRIVERS_FRAMEBUFFER_H
#define LITENIX_DRIVERS_FRAMEBUFFER_H

#include <stdbool.h>
#include <stdint.h>

struct framebuffer_info {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_shift;
    uint8_t green_shift;
    uint8_t blue_shift;
    uint64_t phys_addr;
    uint64_t size;
    void *vaddr;
    bool available;
};

extern struct framebuffer_info g_fb;

bool fb_init(void);

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);
void fb_hline(uint32_t x, uint32_t y, uint32_t w, uint32_t color);
void fb_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_clear(uint32_t color);
uint32_t fb_getpixel(uint32_t x, uint32_t y);

bool fb_self_test(void);

#endif
