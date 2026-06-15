#include <arch/x86_64/limine.h>
#include <drivers/framebuffer.h>
#include <drivers/fbcon.h>
#include <kernel/printk.h>
#include <lib/string.h>
#include <stdint.h>

extern volatile struct limine_hhdm_request hhdm_request;

#define FBCON_CHAR_W 8
#define FBCON_CHAR_H 16

static uint8_t g_font[256][FBCON_CHAR_H];
static uint32_t g_cursor_x;
static uint32_t g_cursor_y;
static uint32_t g_fg = 0x00FFFFFF;
static uint32_t g_bg = 0x00000000;

static uint32_t cols(void) { return g_fb.width / FBCON_CHAR_W; }
static uint32_t rows(void) { return g_fb.height / FBCON_CHAR_H; }

static void draw_glyph(char c, uint32_t cx, uint32_t cy)
{
    uint8_t ch = (uint8_t)c;
    uint32_t px = cx * FBCON_CHAR_W;
    uint32_t py = cy * FBCON_CHAR_H;
    for (uint32_t row = 0; row < FBCON_CHAR_H; row++) {
        uint8_t bits = g_font[ch][row];
        uint32_t *fb_row = (uint32_t *)((uint8_t *)g_fb.vaddr + (py + row) * g_fb.pitch) + px;
        for (uint32_t col = 0; col < FBCON_CHAR_W; col++) {
            *fb_row++ = (bits & (0x80u >> col)) ? g_fg : g_bg;
        }
    }
}

static void scroll_up(void)
{
    if (rows() == 0) return;
    uint32_t h_rows = (rows() - 1) * FBCON_CHAR_H;
    memcpy(g_fb.vaddr, (uint8_t *)g_fb.vaddr + FBCON_CHAR_H * g_fb.pitch, h_rows * g_fb.pitch);
    fb_fillrect(0, h_rows, g_fb.width, FBCON_CHAR_H, g_bg);
}

void fbcon_init(void)
{
    if (!g_fb.available) return;

    int font_src = 0;
    if (hhdm_request.response != 0) {
        uint8_t *bios = (uint8_t *)(uintptr_t)(hhdm_request.response->offset + 0xC0000);
        if (bios[0x7FFE] == 0x55 && bios[0x7FFF] == 0xAA) {
            for (int i = 0; i < 256; i++) {
                for (int j = 0; j < FBCON_CHAR_H; j++) {
                    g_font[i][j] = bios[i * FBCON_CHAR_H + j];
                }
            }
            font_src = 1;
        }
    }

    if (g_font[0x20][0] != 0x00 || g_font[0x20][7] != 0x00) {
        memset(g_font, 0, sizeof(g_font));
        static const uint8_t qmark[FBCON_CHAR_H] = {
            0x00, 0x00, 0x3C, 0x66, 0x6E, 0x7E, 0x76, 0x60,
            0x60, 0x60, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00,
        };
        for (int j = 0; j < FBCON_CHAR_H; j++) g_font['?'][j] = qmark[j];
        font_src = 2;
    }
    printk("FB: font source=%d (0=raw/unchecked, 1=bios, 2=fallback)\n", font_src);

    g_cursor_x = 0;
    g_cursor_y = 0;
    fb_clear(g_bg);
}

void fbcon_putc(char c)
{
    if (!g_fb.available) return;
    if (c == '\n') {
        g_cursor_x = 0;
        g_cursor_y++;
    } else if (c == '\r') {
        g_cursor_x = 0;
    } else {
        draw_glyph(c, g_cursor_x, g_cursor_y);
        g_cursor_x++;
    }

    if (g_cursor_x >= cols()) {
        g_cursor_x = 0;
        g_cursor_y++;
    }
    if (g_cursor_y >= rows()) {
        scroll_up();
        g_cursor_y = rows() - 1;
    }
}

void fbcon_puts(const char *s)
{
    while (*s) fbcon_putc(*s++);
}
