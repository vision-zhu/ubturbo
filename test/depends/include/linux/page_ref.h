/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGE_REF_H
#define _LINUX_PAGE_REF_H

#include <linux/mm_types.h>

static inline int page_ref_count(const struct page *page)
{
	return page->_refcount;
}

static inline int folio_ref_count(const struct folio *folio)
{
	return page_ref_count(&folio->page);
}

static inline int page_count(struct page *page)
{
	return 0;
}

static inline bool folio_try_get(struct folio *folio)
{
    return true;
}

#endif
