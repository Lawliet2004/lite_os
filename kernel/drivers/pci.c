#include <drivers/pci.h>
#include <arch/x86_64/io.h>
#include <kernel/printk.h>

#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA    0xCFC

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address = ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFC) |
                       ((uint32_t)0x80000000);
    outl(CONFIG_ADDRESS, address);
    return inl(CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t address = ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFC) |
                       ((uint32_t)0x80000000);
    outl(CONFIG_ADDRESS, address);
    outl(CONFIG_DATA, value);
}

static struct pci_device pci_devices[32];
static int pci_device_count = 0;

void pci_init(void)
{
    printk("PCI: Scanning bus...\n");
    pci_device_count = 0;

    for (int bus = 0; bus < 8; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t reg0 = pci_read_config(bus, slot, 0, 0);
            uint16_t vendor_id = reg0 & 0xFFFF;
            if (vendor_id == 0xFFFF || vendor_id == 0) continue;

            uint16_t device_id = (reg0 >> 16) & 0xFFFF;
            uint32_t bar0 = pci_read_config(bus, slot, 0, 0x10);
            uint32_t bar1 = pci_read_config(bus, slot, 0, 0x14);
            uint32_t reg15 = pci_read_config(bus, slot, 0, 0x3C);
            uint8_t irq_line = reg15 & 0xFF;

            printk("  PCI: %02x:%02x.0 - vendor=%04x device=%04x bar0=%08x irq=%d\n",
                   bus, slot, vendor_id, device_id, bar0, irq_line);

            if (pci_device_count < 32) {
                struct pci_device *dev = &pci_devices[pci_device_count++];
                dev->bus = bus;
                dev->slot = slot;
                dev->func = 0;
                dev->vendor_id = vendor_id;
                dev->device_id = device_id;
                dev->bar0 = bar0;
                dev->bar1 = bar1;
                dev->irq_line = irq_line;
            }
        }
    }
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device *out_dev)
{
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id && pci_devices[i].device_id == device_id) {
            *out_dev = pci_devices[i];

            /* Enable Bus Master and I/O Space */
            uint32_t cmd = pci_read_config(out_dev->bus, out_dev->slot, out_dev->func, 0x04);
            cmd |= (1 << 2) | (1 << 0); /* Bus Master | I/O Space */
            pci_write_config(out_dev->bus, out_dev->slot, out_dev->func, 0x04, cmd);

            return true;
        }
    }
    return false;
}
