#include "pcie.h"
#include "fs/file_io.h"

#include <memory/heap/kheap.h>
#include <paging/paging.h>

#include <acpi/uacpi/acpi.h>
#include <acpi/uacpi/tables.h>
#include <acpi/uacpi/uacpi.h>

#include <util/assert.h>

#include <stdio.h>
#include <string.h>

#include <pci/pci_ids.h>

#include <autoconf.h>

/*
    What do we do with PCIe?

    > find PCIe devices (i'm gonna read the papers properly this time)
        > find the MCFG table
        > get the ECAM(s)
        > for each ecam:
            > go through all of the buses, devices, functions
            > add the real PCIe devices to the list
            > [TODO] add the PCIe device name string, i don't care for now X3
    > have some API that devs can interact with
*/

pcie_device_t *pcie_list = NULL;

// which functions?
// check if a PCIe device is real or not, or if it's multifunction
/*
    @return NULLPTR if no pointer to header is given
    @return ILLEGAL if illegal vendor is found
    @return MULTIFUN if device is multifunction
    @return OK If device isn't multifunction
*/
pcie_status check_pcie_device(pcie_header_t *header) {
    if (!header) {
        return PCIE_STATUS_NULLPTR;
    }

    if (header->vendor_id == PCIE_ILLEGAL_VENDOR) {
        return PCIE_STATUS_ILLEGAL;
    }

    if (header->header_type & (1 << 7)) {
        return PCIE_STATUS_MULTIFUN;
    }

    return PCIE_STATUS_OK;
}

// append a device to the list
pcie_status pcie_append_to_list(pcie_device_t **list, pcie_device_t *dev) {
    if (!list) {
        return EFAULT;
    }

    if (!(*list)) {
        *list = dev;
        return PCIE_STATUS_OK;
    }

    for (pcie_device_t *d = *list; d != NULL; d = d->next) {
        if (!d->next) {
            d->next = dev;
            break;
        }
    }

    return PCIE_STATUS_OK;
}

// create the pcie_device struct to add to the list
// @param pcie_cfgaddr the PHYSICAL address to the actual PCIe configuration
// space
pcie_status add_pcie_device(pcie_header_t *header, void *pcie_cfgaddr,
                            uint8_t bus_range, fileio_t *pci_ids) {
    if (!header || !pcie_cfgaddr) {
        return PCIE_STATUS_NULLPTR;
    }

    if (!pci_ids) {
        debugf_warn("No pci.ids path given!\n");
        return PCIE_STATUS_NULLPTR;
    }

    pcie_device_t *dev = kmalloc(sizeof(pcie_device_t));
    if (!dev) {
        debugf_warn("Null pointer!\n");
        return PCIE_STATUS_NULLPTR;
    }
    memset(dev, 0, sizeof(pcie_device_t));

    dev->device   = (uint8_t)(((size_t)pcie_cfgaddr >> 15) & 0x1f);
    dev->function = (uint8_t)(((size_t)pcie_cfgaddr >> 12) & 0x7);
    dev->bus      = (uint8_t)(((size_t)pcie_cfgaddr >> 20) & bus_range);

    dev->vendor_id     = header->vendor_id;
    dev->device_id     = header->device_id;
    dev->class_code    = header->class_code;
    dev->subclass_code = header->subclass_code;

    dev->vendor_str = kmalloc(PCIE_MAX_VENDOR_NAME);
    dev->device_str = kmalloc(PCIE_MAX_DEVICE_NAME);

    get_pcix_vendor_device_name(dev->vendor_id, dev->device_id, pci_ids,
                                dev->vendor_str, dev->device_str);

    dev->revision = header->revision_id;

    dev->header_type = header->header_type;

    switch ((dev->header_type & 0b11)) {
    case PCIE_HEADER_T0:
        pcie_header0_t *h0 = (pcie_header0_t *)(header + sizeof(pcie_header_t));

        dev->bars = kcalloc(PCIE_HEADT0_BARS, sizeof(uint32_t));

        for (int i = 0; i < PCIE_HEADT0_BARS; i++) {
            dev->bars[i] = h0->bars[i];
        }

        dev->irq_line = h0->irq_line;
        dev->irq_pin  = h0->irq_pin;
        break;

    case PCIE_HEADER_T1:
        pcie_header1_t *h1 = (pcie_header1_t *)(header + sizeof(pcie_header_t));

        dev->bars = kcalloc(PCIE_HEADT1_BARS, sizeof(uint32_t));

        for (int i = 0; i < PCIE_HEADT1_BARS; i++) {
            dev->bars[i] = h1->bars[i];
        }

        dev->irq_line = h1->irq_line;
        dev->irq_pin  = h1->irq_pin;
        break;

    default:
        break;
    }

    kprintf_info("Device [%.02hhx:%.02hhx.%.01hhx] OK!\n", dev->bus,
                 dev->device, dev->function);

    return pcie_append_to_list(&pcie_list, dev);
}

// iterate through the ECAM
// check all buses
pcie_status pcie_parse_ecam(struct acpi_mcfg_allocation *ecam,
                            const char *pciids_path) {
    if (!ecam) {
        return PCIE_STATUS_NULLPTR;
    }

    if (!pciids_path) {
        debugf_warn("No pci.ids path given!\n");
        return PCIE_STATUS_NULLPTR;
    }

    fileio_t *pci_ids = open(pciids_path, 0, 0);
    assert(pci_ids);

    uint64_t ecam_base = ecam->address;

    uint8_t bus_start = ecam->start_bus;
    uint8_t bus_end   = ecam->end_bus;

    for (uint16_t bus = bus_start; bus < bus_end + 1; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint64_t addr = ecam_base + PCIE_OFFSET(bus, device, function);

                uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRTUAL(_get_pml4());

                map_region(pml4, addr, PHYS_TO_VIRTUAL(addr), 1,
                           PMLE_KERNEL_READ_WRITE);

#ifdef CONFIG_PCIE_DEBUG
                debugf("[%.02hhx:%.02hhx.%.01hhx] Checking %llx\n", bus, device,
                       function, addr);
#endif

                pcie_header_t *header = (pcie_header_t *)PHYS_TO_VIRTUAL(addr);

                switch (check_pcie_device(header)) {
                case PCIE_STATUS_ILLEGAL:
                    unmap_region(pml4, PHYS_TO_VIRTUAL(addr), 1);
#ifdef CONFIG_PCIE_DEBUG
                    debugf(
                        "[%.02hhx:%.02hhx.%.01hhx] Nope, not a PCIe device\n",
                        bus, device, function);
#endif
                    continue; // don't add the device :meow:

                case PCIE_STATUS_MULTIFUN:
                    break;

                case PCIE_STATUS_OK:
                    // only one function
                    function = 8;
                    break;

                default:
                    debugf_warn("Something went wrong when checking the PCIe "
                                "device type!\n");
                    continue;
                }

#ifdef CONFIG_PCIE_DEBUG
                debugf("[%.02hhx:%.02hhx.%.01hhx] Ok we're good\n", bus, device,
                       function);
#endif

                if (add_pcie_device(header, (void *)addr, bus_end - bus_start,
                                    pci_ids) != PCIE_STATUS_OK) {
                    debugf_warn(
                        "Couldn't parse device [%.02hhx:%.02hhx.%.01hhx]\n",
                        bus, device, function);

                    continue;
                }
            }
        }
    }

    close(pci_ids);

    return PCIE_STATUS_OK;
}

// PCIe init
pcie_status pcie_init(const char *pciids_path) {
    if (!pciids_path) {
        debugf_warn("No pci.ids path given!\n");
        return PCIE_STATUS_NULLPTR;
    }

    struct uacpi_table *table = kmalloc(sizeof(struct uacpi_table));
    uacpi_table_find_by_signature(ACPI_MCFG_SIGNATURE, table);

    if (!table) {
        debugf_warn("Couldn't find the MCFG table!\n");
        return PCIE_STATUS_NULLPTR;
    }

    struct acpi_mcfg *mcfg_table = table->ptr;

    int ecam_count = (mcfg_table->hdr.length - sizeof(struct acpi_sdt_hdr)) /
                     sizeof(struct acpi_mcfg_allocation);

    if (ecam_count < 1) {
        kprintf_warn("No ECAM spaces found!\n");
        return PCIE_STATUS_ENOCFG;
    }

    for (int idx = 0; idx < ecam_count; idx++) {
        struct acpi_mcfg_allocation ecam = mcfg_table->entries[idx];

        switch (pcie_parse_ecam(&ecam, pciids_path)) {
        case PCIE_STATUS_OK:
            break;

        default:
            kfree(table);
            return PCIE_STATUS_EUNKNOWN;
        }
    }

    kfree(table);
    return PCIE_STATUS_OK;
}

void print_pcie_list() {
    for (pcie_device_t *p = pcie_list; p != NULL; p = p->next) {
        debugf("PCIE[%.02hhx:%.02hhx.%.01hhx](%hx:%hx) %s %s\n", p->bus,
               p->device, p->function, p->vendor_id, p->device_id,
               p->vendor_str, p->device_str);
    }
}
