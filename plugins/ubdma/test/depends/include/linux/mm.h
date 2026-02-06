/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <asm/page.h>
#include <linux/gfp.h>
#include <linux/mmzone.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/page_ref.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/nodemask.h>

#define PAGE_SIZE        (_AC(1, UL) << PAGE_SHIFT)

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

static inline bool folio_test_large(struct folio *folio)
{
    return false;
}
static inline unsigned int folio_order(struct folio *folio)
{
    if (!folio_test_large(folio)) {
        return 0;
    }
    return folio->_flags_1 & 0xff;
}

static inline size_t folio_size(struct folio *folio)
{
    return PAGE_SIZE << folio_order(folio);
}

void folio_put(struct folio *folio);
struct folio *get_hugetlb_folio_nodemask(unsigned long size, int preferred_nid,
    nodemask_t *nmask, gfp_t gfp_mask, void *data);
bool folio_test_hugetlb_migratable(struct folio *folio);

#endif // _LINUX_MM_H
