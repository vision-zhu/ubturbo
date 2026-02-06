/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Macros for manipulating and testing page->flags
 */

#ifndef PAGE_FLAGS_H
#define PAGE_FLAGS_H

#include <linux/mm_types.h>


#ifdef __cplusplus
extern "C" {
#endif

struct page *compound_head(struct page *page);
extern unsigned long _compound_head(struct page *page);

static inline bool folio_test_hugetlb(struct folio *folio)
{
    return false;
}

#define page_folio(p)		(_Generic((p),				\
    const struct page *:	(const struct folio *)_compound_head(p), \
    struct page *:		(struct folio *)_compound_head(p)))

#ifdef __cplusplus
}
#endif

#endif // PAGE_FLAGS_H
