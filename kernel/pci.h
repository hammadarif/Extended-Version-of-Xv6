// pci.h - PCI header
#ifndef PCI_H
#define PCI_H

#include "types.h"


// Function prototypes
uint32 pci_config_read32(uint8 bus, uint8 slot, uint8 func, uint8 offset);
int pci_find_device(uint16 vendor_id, uint16 device_id);
void pci_init(void);

#endif // PCI_H
