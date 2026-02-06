/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MIGRATE_H
#define _LINUX_MIGRATE_H

#include <linux/mm_types.h>
#include <linux/migrate_mode.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct folio *new_folio_t(struct folio *folio, unsigned long private_);
typedef void free_folio_t(struct folio *folio, unsigned long private_);
#ifdef __cplusplus
}
#endif

#endif /* _LINUX_MIGRATE_H */
