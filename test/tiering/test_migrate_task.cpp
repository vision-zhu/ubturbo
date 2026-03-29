/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: smap migrate task module test code
 */

#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

#include <linux/slab.h>

#include "migrate_task.h"
#include "migrate_back_debugfs.h"

using namespace std;

class MigrateTaskTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

extern "C" struct migrate_back_task *init_migrate_back_task(unsigned long long task_id);
TEST_F(MigrateTaskTest, InitMigrateBackTask)
{
    struct migrate_back_task *ret = init_migrate_back_task(10);
    ASSERT_NE(nullptr, ret);
    EXPECT_EQ(10, ret->task_id);
    kfree(ret);
}

extern "C" void free_all_migrate_back_task(void);
TEST_F(MigrateTaskTest, FreeAllMigrateBackTask)
{
    free_all_migrate_back_task();
}

extern "C" int init_migrate_back_subtask(struct migrate_back_task *task,
    struct migrate_back_inner_payload *p, struct migrate_back_subtask **result);
TEST_F(MigrateTaskTest, InitMigrateBackSubtask)
{
    int ret;
    struct migrate_back_task task;
    struct migrate_back_inner_payload p;
    struct migrate_back_subtask **result = nullptr;
    result = static_cast<struct migrate_back_subtask**>(kmalloc(sizeof(*result), GFP_KERNEL));
    ASSERT_NE(nullptr, result);

    p.src_nid = 1;
    ret = init_migrate_back_subtask(&task, &p, result);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(1, result[0]->src_nid);

    kfree(*result);
    kfree(result);
}

TEST_F(MigrateTaskTest, InitMigrateBackSubtaskTwo)
{
    int ret;
    struct migrate_back_task task;
    struct migrate_back_inner_payload p;
    struct migrate_back_subtask **result = nullptr;
    result = static_cast<struct migrate_back_subtask**>(kmalloc(sizeof(*result), GFP_KERNEL));
    ASSERT_NE(nullptr, result);

    p.src_nid = 1;
    MOCKER(kzalloc).stubs().will(returnValue((void*)NULL));
    ret = init_migrate_back_subtask(&task, &p, result);
    EXPECT_EQ(-ENOMEM, ret);

    kfree(result);
}

extern "C" void clear_migrate_back_task(void);
TEST_F(MigrateTaskTest, ClearMigrateBackTask)
{
    clear_migrate_back_task();
}

TEST_F(MigrateTaskTest, FreeAllMigrateBackTask_WithEntries)
{
    struct migrate_back_task *t1 = init_migrate_back_task(1);
    struct migrate_back_task *t2 = init_migrate_back_task(2);
    ASSERT_NE(nullptr, t1);
    ASSERT_NE(nullptr, t2);

    struct migrate_back_inner_payload p = { .src_nid = 0, .dest_nid = 1 };
    struct migrate_back_subtask *sub = nullptr;
    ASSERT_EQ(0, init_migrate_back_subtask(t1, &p, &sub));
    list_add(&sub->task_list, &t1->subtask);

    spin_lock(&migrate_back_task_lock);
    list_add(&t1->task_node, &migrate_back_task_list);
    list_add(&t2->task_node, &migrate_back_task_list);
    spin_unlock(&migrate_back_task_lock);

    /* free_all_migrate_back_task frees memory but does not reset list heads;
     * re-initialize the global list head to leave a clean state. */
    free_all_migrate_back_task();
    INIT_LIST_HEAD(&migrate_back_task_list);
}

TEST_F(MigrateTaskTest, ClearMigrateBackTask_PrunesOldDoneTasks)
{
    /* Fill list with 101 WAITING tasks + 1 DONE task (102 total).
     * clear_migrate_back_task prunes entries beyond index 100 that have
     * status > MB_TASK_WAITING, so the last DONE task should be freed. */
    const int TOTAL = 102;
    for (int i = 0; i < TOTAL; i++) {
        struct migrate_back_task *t = init_migrate_back_task(i);
        ASSERT_NE(nullptr, t);
        t->status = (i < TOTAL - 1) ? MB_TASK_WAITING : MB_TASK_DONE;
        spin_lock(&migrate_back_task_lock);
        list_add_tail(&t->task_node, &migrate_back_task_list);
        spin_unlock(&migrate_back_task_lock);
    }

    MOCKER(remove_migrate_back_debugfs).stubs().will(ignoreReturnValue());
    clear_migrate_back_task();

    int remaining = 0;
    struct migrate_back_task *t, *tmp;
    list_for_each_entry_safe(t, tmp, &migrate_back_task_list, task_node) {
        remaining++;
        list_del(&t->task_node);
        kfree(t);
    }
    EXPECT_LT(remaining, TOTAL);
    INIT_LIST_HEAD(&migrate_back_task_list);
}
