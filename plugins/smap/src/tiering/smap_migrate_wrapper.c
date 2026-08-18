// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 * Description: SMAP Tiering Memory Solution: wrapper
 */

#include <linux/nodemask.h>
#include <linux/kprobes.h>

#include "smap_migrate_wrapper.h"

DEFINE_STATIC_KEY_FALSE(cpusets_pre_enable_key);
DEFINE_STATIC_KEY_FALSE(cpusets_enabled_key);

unsigned long (*fp_kallsyms_lookup_name)(const char *) = NULL;
int (*fp_migrate_pages)(struct list_head *from, new_folio_t get_new_folio,
			free_folio_t put_new_folio, unsigned long priv,
			enum migrate_mode mode, int reason,
			unsigned int *ret_succeeded) = NULL;
void (*fp_putback_movable_pages)(struct list_head *l) = NULL;
bool (*fp_isolate_folio_to_list)(struct folio *folio,
				 struct list_head *list) = NULL;
unsigned long (*fp_reclaim_pages)(struct list_head *folio_list,
				  bool ignore_references) = NULL;
bool (*fp_isolate_hugetlb)(struct folio *folio, struct list_head *list) = NULL;
void (*fp_folio_putback_lru)(struct folio *folio) = NULL;
bool (*fp_folio_isolate_lru)(struct folio *folio) = NULL;
bool (*fp_folio_free_swap)(struct folio *folio) = NULL;

void lookup_kallsyms_lookup_name(void)
{
	struct kprobe kp;
	memset(&kp, 0, sizeof(struct kprobe));
	kp.symbol_name = "kallsyms_lookup_name";
	if (register_kprobe(&kp) < 0) {
		return;
	}
	fp_kallsyms_lookup_name = (unsigned long (*)(const char *))kp.addr;
	unregister_kprobe(&kp);
}

int smap_process_symbols(void)
{
	lookup_kallsyms_lookup_name();
	if (!fp_kallsyms_lookup_name)
		return -EFAULT;

	// clang-format off
	fp_migrate_pages =
		(int (*)(struct list_head *from, new_folio_t get_new_folio,
			 free_folio_t put_new_folio, unsigned long priv,
			 enum migrate_mode mode, int reason,
			 unsigned int *ret_succeeded))
			fp_kallsyms_lookup_name("migrate_pages");
	fp_putback_movable_pages =
		(void (*)(struct list_head *l))
			fp_kallsyms_lookup_name("putback_movable_pages");
	fp_isolate_folio_to_list =
		(bool (*)(struct folio *folio, struct list_head *list))
			fp_kallsyms_lookup_name("isolate_folio_to_list");
	// clang-format on
	if (!(fp_migrate_pages && fp_putback_movable_pages &&
	      fp_isolate_folio_to_list))
		return -EFAULT;

	/* Reclaim-path symbols for swap-out */
	fp_reclaim_pages = (unsigned long (*)(struct list_head *, bool))
		fp_kallsyms_lookup_name("reclaim_pages");
	fp_isolate_hugetlb = (bool (*)(struct folio *, struct list_head *))
		fp_kallsyms_lookup_name("isolate_hugetlb");
	fp_folio_putback_lru = (void (*)(
		struct folio *))fp_kallsyms_lookup_name("folio_putback_lru");
	fp_folio_isolate_lru = (bool (*)(
		struct folio *))fp_kallsyms_lookup_name("folio_isolate_lru");
	fp_folio_free_swap = (bool (*)(
		struct folio *))fp_kallsyms_lookup_name("folio_free_swap");
	if (!fp_reclaim_pages || !fp_isolate_hugetlb || !fp_folio_putback_lru ||
	    !fp_folio_isolate_lru || !fp_folio_free_swap)
		pr_warn("smap: swap-out symbols missing (reclaim_pages=%p "
			"isolate_hugetlb=%p folio_putback_lru=%p "
			"folio_isolate_lru=%p)\n",
			fp_reclaim_pages, fp_isolate_hugetlb,
			fp_folio_putback_lru, fp_folio_isolate_lru);

	return 0;
}

/* get_pfnblock_flags_mask and its helpers now imported from drivers */
