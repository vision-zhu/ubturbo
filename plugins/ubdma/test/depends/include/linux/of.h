/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_OF_H
#define _LINUX_OF_H
struct device_node {
    const char *name;
    const char *full_name;
    struct device_node *parent;
    struct device_node *child;
    struct device_node *sibling;
#if defined(CONFIG_OF_KOBJ)
    struct kobject kobj;
#endif
    unsigned long _flags;
    void *data;
#if defined(CONFIG_SPARC)
    unsigned int unique_id;
#endif
};
#endif