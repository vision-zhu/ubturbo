// SPDX-License-Identifier: GPL-2.0-only
/*
 * Description: SMAP work module
 */

#include "smap_msg.h"
#include "migrate_task.h"
#include "migrate_back_debugfs.h"
#include "migrate_main.h"
#include "work.h"

#define MAX_MIGRATE_BATCH_SIZE 100

int start_migrate_back_work(void)
{
	int cnt;
	bool flag;
	struct migrate_back_task *task_list[MAX_MIGRATE_BATCH_SIZE];
	struct migrate_back_task *t;

	do {
		flag = false;
		cnt = 0;
		spin_lock(&migrate_back_task_lock);
		list_for_each_entry(t, &migrate_back_task_list, task_node) {
			if (t->status >= MB_TASK_WAITING)
				continue;
			if (cnt >= MAX_MIGRATE_BATCH_SIZE) {
				flag = true;
				break;
			}
			t->status = MB_TASK_WAITING;
			/* keep the task alive while its file is created
			 * outside the lock (a concurrent clear may unlist it) */
			kref_get(&t->ref);
			task_list[cnt++] = t;
		}
		spin_unlock(&migrate_back_task_lock);

		while (cnt) {
			t = task_list[--cnt];
			create_migrate_back_debugfs(t);
			kref_put(&t->ref, migrate_back_task_release);
		}
	} while (flag);
	return 0;
}
