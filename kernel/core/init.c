#include <arch/x86_64/limine.h>
#include <kernel/compiler.h>

USED SECTION(".limine_requests_start")
static volatile uint64_t limine_requests_start_marker[4] = {
    LIMINE_COMMON_MAGIC_0,
    LIMINE_COMMON_MAGIC_1,
    0,
    0,
};

USED SECTION(".limine_requests")
volatile struct limine_bootloader_info_request bootloader_info_request = {
    .id = {
        LIMINE_COMMON_MAGIC_0,
        LIMINE_COMMON_MAGIC_1,
        LIMINE_BOOTLOADER_INFO_REQUEST_ID_0,
        LIMINE_BOOTLOADER_INFO_REQUEST_ID_1,
    },
    .revision = 0,
    .response = 0,
};

USED SECTION(".limine_requests")
volatile struct limine_memmap_request memmap_request = {
    .id = {
        LIMINE_COMMON_MAGIC_0,
        LIMINE_COMMON_MAGIC_1,
        LIMINE_MEMMAP_REQUEST_ID_0,
        LIMINE_MEMMAP_REQUEST_ID_1,
    },
    .revision = 0,
    .response = 0,
};

USED SECTION(".limine_requests")
volatile struct limine_hhdm_request hhdm_request = {
    .id = {
        LIMINE_COMMON_MAGIC_0,
        LIMINE_COMMON_MAGIC_1,
        LIMINE_HHDM_REQUEST_ID_0,
        LIMINE_HHDM_REQUEST_ID_1,
    },
    .revision = 0,
    .response = 0,
};

USED SECTION(".limine_requests_end")
static volatile uint64_t limine_requests_end_marker[2] = { 0, 0 };
