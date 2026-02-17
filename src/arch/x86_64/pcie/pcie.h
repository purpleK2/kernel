#ifndef PCIE_H
#define PCIE_H 1

#include <stddef.h>
#include <stdint.h>

#include <errors.h>

#define PCIE_HEADT0_BARS 6
#define PCIE_HEADT1_BARS 2

#define PCIE_MAX_VENDOR_NAME 128
#define PCIE_MAX_DEVICE_NAME 128

#define PCIE_ILLEGAL_VENDOR 0xFFFF

// 4096 bytes for per-PCIe device Configuration Space
#define PCIE_CFGSPACE_PAGES 1

#define PCI_IDS_PATH "/initrd/pci.ids"

#define PCIE_OFFSET(b, d, f) ((b << 20) | (d << 15) | (f << 12))
#define PCIE_BAR_ADDR(b)     (b & ~(0b1111))

typedef enum {
    PCIE_STATUS_OK       = EOK,
    PCIE_STATUS_ENOCFG   = -EINVAL,
    PCIE_STATUS_NULLPTR  = -EFAULT,
    PCIE_STATUS_ILLEGAL  = -9,
    PCIE_STATUS_EUNKNOWN = -ENOTRECOVERABLE,
    PCIE_STATUS_EINVALID = -EINVAL,
    PCIE_STATUS_MULTIFUN = 1,
} pcie_status;

typedef enum {
    // bit 0
    PCIE_BAR_MMIO = 0,
    PCIE_BAR_PIO  = 1,

    // bits 1-2
    PCIE_BAR_MM32 = 0,
    PCIE_BAR_MM64 = 2,
} pcie_bar_flags;

typedef enum {
    PCIE_HEADER_T0 = 0,
    PCIE_HEADER_T1 = 1
} pcie_header_type;

typedef struct pcie_header {
    uint16_t vendor_id;
    uint16_t device_id;

    uint16_t status_reg;
    uint16_t command_reg;

    uint8_t class_code;
    uint8_t subclass_code;
    uint8_t programming_interface;

    uint8_t revision_id;

    uint8_t bist; // what the heck is this

    uint8_t header_type;

    uint8_t plt; // Primary Latency Timer
    uint8_t cache_line_size;
} pcie_header_t;

typedef struct pcie_header0 {
    uint32_t bars[PCIE_HEADT0_BARS];

    uint32_t reserved0;

    uint16_t subsystem_id;
    uint16_t subsystem_vendor_id;

    char reserved1[7];

    uint8_t capabilities_ptr;

    uint16_t reserved2[6]; // we don't need MAX_LAT not MAX_GNT

    uint8_t irq_pin;
    uint8_t irq_line;
} pcie_header0_t;

typedef struct pcie_header1 {
    uint32_t bars[PCIE_HEADT1_BARS];

    uint8_t slt; // Secondary Latency Timer

    uint8_t subordinate_busnumber;
    uint8_t secondary_busnumber;
    uint8_t primary_busnumber;

    uint16_t secondary_status_reg;

    uint8_t io_limit_lo;
    uint8_t io_base_lo;

    uint16_t memory_limit;
    uint16_t memory_base;

    uint16_t prefetchable_limit_lo;
    uint16_t prefetchable_base_lo;

    uint32_t prefetchable_base_hi;
    uint32_t prefetchable_limit_hi;

    uint16_t io_limit_hi;
    uint16_t io_base_hi;

    uint8_t reserved[3];

    uint8_t capabilities_ptr;

    uint32_t expansionrom_base;

    uint16_t bridge_control;

    uint8_t irq_pin;
    uint8_t irq_line;
} pcie_header1_t;

typedef struct pcie_device {
    uint8_t bus, device, function;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass_code;

    uint8_t revision;

    pcie_header_type header_type;

    char *vendor_str;
    char *device_str;

    // Header type-specific attributes

    uint32_t *bars;

    uint8_t irq_line, irq_pin;

    struct pcie_device *next;
} pcie_device_t;

pcie_status pcie_init(const char *pciids_path);

void print_pcie_list();

#endif