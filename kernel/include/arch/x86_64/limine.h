#ifndef LITENIX_ARCH_X86_64_LIMINE_H
#define LITENIX_ARCH_X86_64_LIMINE_H

#include <kernel/compiler.h>
#include <stdint.h>

#define LIMINE_COMMON_MAGIC_0 0xc7b1dd30df4c8b88ULL
#define LIMINE_COMMON_MAGIC_1 0x0a82e883a194f07bULL

#define LIMINE_BOOTLOADER_INFO_REQUEST_ID_0 0xf55038d8e2a1202fULL
#define LIMINE_BOOTLOADER_INFO_REQUEST_ID_1 0x279426fcf5f59740ULL
#define LIMINE_MEMMAP_REQUEST_ID_0 0x67cf3d9d378a806fULL
#define LIMINE_MEMMAP_REQUEST_ID_1 0xe304acdfc50c3c62ULL
#define LIMINE_HHDM_REQUEST_ID_0 0x48dcf1cb8ad2b852ULL
#define LIMINE_HHDM_REQUEST_ID_1 0x63984e959a98244bULL

struct limine_uuid {
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t d[8];
};

struct limine_file {
    uint64_t revision;
    void *address;
    uint64_t size;
    char *path;
    char *cmdline;
    uint32_t media_type;
    uint32_t unused;
    uint32_t tftp_ip;
    uint32_t tftp_port;
    uint32_t partition_index;
    uint32_t mbr_disk_id;
    struct limine_uuid gpt_disk_uuid;
    struct limine_uuid gpt_part_uuid;
    struct limine_uuid part_uuid;
};

struct limine_bootloader_info_response {
    uint64_t revision;
    char *name;
    char *version;
};

struct limine_bootloader_info_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_bootloader_info_response *response;
} PACKED;

struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};

struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
} PACKED;

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
} PACKED;

enum {
    LIMINE_MEMMAP_USABLE = 0,
    LIMINE_MEMMAP_RESERVED = 1,
    LIMINE_MEMMAP_ACPI_RECLAIMABLE = 2,
    LIMINE_MEMMAP_ACPI_NVS = 3,
    LIMINE_MEMMAP_BAD_MEMORY = 4,
    LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE = 5,
    LIMINE_MEMMAP_KERNEL_AND_MODULES = 6,
    LIMINE_MEMMAP_FRAMEBUFFER = 7,
};

#define LIMINE_KERNEL_FILE_REQUEST_ID_0 0xad97e90e83f1ed67ULL
#define LIMINE_KERNEL_FILE_REQUEST_ID_1 0x31eb5d1c5ff23b69ULL
#define LIMINE_FRAMEBUFFER_REQUEST_ID_0 0x9d5827dcd881dd75ULL
#define LIMINE_FRAMEBUFFER_REQUEST_ID_1 0xa3148604f6fab11bULL

struct limine_video_mode {
    uint64_t pitch;
    uint64_t width;
    uint64_t height;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
} PACKED;

struct limine_framebuffer {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    void *edid;
    uint64_t mode_count;
    struct limine_video_mode **modes;
} PACKED;

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

struct limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_framebuffer_response *response;
} PACKED;

struct limine_kernel_file_response {
    uint64_t revision;
    struct limine_file *kernel_file;
};

struct limine_kernel_file_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_kernel_file_response *response;
} PACKED;

extern volatile struct limine_bootloader_info_request bootloader_info_request;
extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_hhdm_request hhdm_request;
extern volatile struct limine_kernel_file_request kernel_file_request;
extern volatile struct limine_framebuffer_request framebuffer_request;

#endif
