/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/migrate.h>

int isolate_and_migrate_folios(struct folio **folios, unsigned int nr_folios,
    new_folio_t get_new_folio, free_folio_t put_new_folio,
    unsigned long private_, enum migrate_mode mode, unsigned int *nr_succeeded)
{
    return 0;
}