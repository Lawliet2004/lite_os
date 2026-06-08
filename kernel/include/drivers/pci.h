#ifndef LITENIX_DRIVERS_PCI_H
#define LITENIX_DRIVERS_PCI_H

#include <stdint.h>
#include <stdbool.h>

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar0;
    uint32_t bar1;
    uint8_t irq_line;
};

void pci_init(void);
bool pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device *out_dev);
uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

#endif
