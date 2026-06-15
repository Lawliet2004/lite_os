#include <arch/x86_64/limine.h>
#include <drivers/framebuffer.h>
#include <stddef.h>

struct framebuffer_info g_fb = { 0 };

bool fb_init(void)
{
    if (framebuffer_request.response == 0) {
        return false;
    }
    if (framebuffer_request.response->framebuffer_count < 1) {
        return false;
    }
    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    if (fb == 0 || fb->address == 0) {
        return false;
    }

    g_fb.width = fb->width;
    g_fb.height = fb->height;
    g_fb.pitch = fb->pitch;
    g_fb.bpp = fb->bpp;
    g_fb.memory_model = fb->memory_model;
    g_fb.red_shift = fb->red_mask_shift;
    g_fb.green_shift = fb->green_mask_shift;
    g_fb.blue_shift = fb->blue_mask_shift;
    g_fb.phys_addr = (uint64_t)(uintptr_t)fb->address;
    g_fb.size = (uint64_t)fb->pitch * fb->height;
    g_fb.vaddr = fb->address;
    g_fb.available = true;

    return true;
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!g_fb.available) return;
    if (x >= g_fb.width || y >= g_fb.height) return;
    volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)g_fb.vaddr + y * g_fb.pitch);
    row[x] = color;
    __asm__ volatile ("clflush (%0)" :: "r"(&row[x]) : "memory");
}

uint32_t fb_getpixel(uint32_t x, uint32_t y)
{
    if (!g_fb.available) return 0;
    if (x >= g_fb.width || y >= g_fb.height) return 0;
    volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)g_fb.vaddr + y * g_fb.pitch);
    return row[x];
}

void fb_hline(uint32_t x, uint32_t y, uint32_t w, uint32_t color)
{
    if (!g_fb.available) return;
    if (y >= g_fb.height) return;
    if (x >= g_fb.width) return;
    if (x + w > g_fb.width) w = g_fb.width - x;
    volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)g_fb.vaddr + y * g_fb.pitch);
    for (uint32_t i = 0; i < w; i++) {
        row[x + i] = color;
    }
}

void fb_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!g_fb.available) return;
    for (uint32_t j = 0; j < h; j++) {
        fb_hline(x, y + j, w, color);
    }
}

void fb_clear(uint32_t color)
{
    if (!g_fb.available) return;
    for (uint32_t y = 0; y < g_fb.height; y++) {
        volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)g_fb.vaddr + y * g_fb.pitch);
        for (uint32_t x = 0; x < g_fb.width; x++) {
            row[x] = color;
        }
    }
}

bool fb_self_test(void)
{
    if (!g_fb.available) return false;
    if (g_fb.width < 32 || g_fb.height < 32) return false;

    fb_fillrect(0, 0, 16, 16, 0x00FF0000);
    uint32_t c = fb_getpixel(8, 8);
    if (c != 0x00FF0000) return false;

    fb_fillrect(16, 0, 16, 16, 0x0000FF00);
    c = fb_getpixel(24, 8);
    if (c != 0x0000FF00) return false;

    fb_fillrect(0, 16, 32, 16, 0x000000FF);
    c = fb_getpixel(16, 24);
    if (c != 0x000000FF) return false;

    return true;
}
