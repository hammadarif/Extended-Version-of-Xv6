// pci.c - PCI support for RISC-V
#include "pci.h"
//#include "defs.h"


extern void printf(char *fmt, ...);
// PCI configuration space base address (typical for RISC-V)
#define PCI_CONFIG_BASE 0x30000000

// Read a 32-bit value from PCI configuration space using MMIO
static uint32 pci_mmio_read32(uint32 address) {
    //return *(volatile uint32 *)(PCI_CONFIG_BASE + address);
    return *(volatile uint32 *)((char *)PCI_CONFIG_BASE + address);
}

// Write a 32-bit value to PCI configuration space using MMIO
static void pci_mmio_write32(uint32 address, uint32 value) {
    //*(volatile uint32 *)(PCI_CONFIG_BASE + address) = value;
    *(volatile uint32 *)((char *)PCI_CONFIG_BASE + address) = value;
}

// Read from PCI configuration space
uint32 pci_config_read32(uint8 bus, uint8 slot, uint8 func, uint8 offset) {
    uint32 address = (1 << 31)               // Enable bit
                       | (bus << 16)          // Bus number
                       | (slot << 11)         // Slot number
                       | (func << 8)          // Function number
                       | (offset & 0xFC);     // Offset, aligned to 4 bytes
    pci_mmio_write32(0x0, address);           // Write to PCI address register
    return pci_mmio_read32(0x4);              // Read from PCI data register
}

// Find a PCI device by vendor and device ID
int pci_find_device(uint16 vendor_id, uint16 device_id) {
    for (uint8 bus = 0; bus < 256; bus++) {
        for (uint8 slot = 0; slot < 32; slot++) {
            uint16 vendor = pci_config_read32(bus, slot, 0, 0) & 0xFFFF;
            if (vendor == vendor_id) {
                uint16 device = (pci_config_read32(bus, slot, 0, 0) >> 16) & 0xFFFF;
                if (device == device_id) {
                    return (bus << 8) | slot;  // Combine bus and slot into a single identifier
                }
            }
        }
    }
    return -1;  // Device not found
}

// Initialize PCI subsystem (for testing)
void pci_init() {
    printf("Initializing PCI...\n");
    int dev_id = pci_find_device(0x1AF4, 0x1000);  // Example: Look for VirtIO device
    if (dev_id >= 0) {
        printf("PCI device found at bus %d, slot %d\n", dev_id >> 8, dev_id & 0xFF);
    } else {
        printf("No PCI device found.\n");
    }
}
