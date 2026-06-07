#include <arch/x86_64/limine.h>
#include <drivers/vga_text.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VGA_TEXT_PHYS_ADDR 0xB8000ULL
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_COLOR_LIGHT_GREY_ON_BLACK 0x07

static size_t row;
static size_t column;
static uint8_t color;
static bool initialized;
static volatile uint16_t *buffer;

static uint16_t vga_entry(char ch)
{
    return (uint16_t)ch | ((uint16_t)color << 8);
}

static void scroll_if_needed(void)
{
    if (row < VGA_HEIGHT) {
        return;
    }

    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            buffer[(y - 1) * VGA_WIDTH + x] =
                buffer[y * VGA_WIDTH + x];
        }
    }

    for (size_t x = 0; x < VGA_WIDTH; x++) {
        buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ');
    }

    row = VGA_HEIGHT - 1;
}

void vga_text_init(void)
{
    if (hhdm_request.response == 0) {
        initialized = false;
        return;
    }

    buffer = (volatile uint16_t *)(uintptr_t)
        (hhdm_request.response->offset + VGA_TEXT_PHYS_ADDR);
    row = 0;
    column = 0;
    color = VGA_COLOR_LIGHT_GREY_ON_BLACK;
    initialized = true;

    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            buffer[y * VGA_WIDTH + x] = vga_entry(' ');
        }
    }
}

void vga_text_write_char(char ch)
{
    if (!initialized) {
        return;
    }

    if (ch == '\n') {
        column = 0;
        row++;
        scroll_if_needed();
        return;
    }

    buffer[row * VGA_WIDTH + column] = vga_entry(ch);
    column++;

    if (column >= VGA_WIDTH) {
        column = 0;
        row++;
        scroll_if_needed();
    }
}

void vga_text_write_string(const char *text)
{
    while (*text != '\0') {
        vga_text_write_char(*text++);
    }
}
