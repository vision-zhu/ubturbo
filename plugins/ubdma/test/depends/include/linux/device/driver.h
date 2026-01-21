/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TEST_DRIVER_H
#define TEST_DRIVER_H

#include <linux/types.h>

#define ACPI_ID_LEN	16
struct acpi_device_id {
    __u8 id[ACPI_ID_LEN];
    __u32 cls;
    __u32 cls_msk;
};

struct device;
struct device_driver {
    /* depends acpi_match_table */
    struct acpi_device_id *acpi_match_table;
    char reserved1;
    /* depends remove */
    int (*remove) (struct device *dev);
    char reserved2;
    /* depends probe */
    int (*probe) (struct device *dev);
    char reserved3;
    const char *mod_name;
    char reserved4;
    struct module *owner;
    char reserved5;
    struct bus_type *bus;
    char reserved6;
    const char *name;
    char reserved7;
};


#endif // TEST_DRIVER_H
