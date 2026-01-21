/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _LINUX_MEMORY_HOTPLUG_H
#define _LINUX_MEMORY_HOTPLUG_H
#include <linux/migrate_mode.h>
#ifdef CONFIG_MEMORY_HOTPLUG
#else
#define pfn_to_online_page(pfn) \
    ({ \
        struct page *___page = NULL; \
        if (pfn_valid(pfn)) \
            ___page = pfn_to_page(pfn); \
        ___page; \
    })
#endif

#endif
