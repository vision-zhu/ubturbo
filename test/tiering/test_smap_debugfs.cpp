/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 * Description: SMAP Debugfs Test
 */

#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

#include <asm/page-def.h>
#include <linux/slab.h>

#include "smap_debugfs.h"
#include "tracking_manage.h"

using namespace std;

class TestSmapDebugfs : public ::testing::Test {
protected:
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

extern "C" {
int smap_debugfs_migrate_init(void);
void smap_debugfs_mod_exit(void);
bool IS_ERR(const void *ptr);
long PTR_ERR(const void *ptr);
};

TEST_F(TestSmapDebugfs, SmapDebugfsMigrateInitSuccess)
{
    MOCKER(IS_ERR).stubs().will(returnValue(false));
    int ret = smap_debugfs_migrate_init();
    EXPECT_EQ(0, ret);
    smap_debugfs_mod_exit();
}

TEST_F(TestSmapDebugfs, SmapDebugfsMigrateInitDirCreateFailed)
{
    MOCKER(IS_ERR).stubs().will(returnValue(true));
    MOCKER(PTR_ERR).stubs().will(returnValue(-ENOMEM));
    int ret = smap_debugfs_migrate_init();
    EXPECT_EQ(-ENOMEM, ret);
}

TEST_F(TestSmapDebugfs, SmapDebugfsModExitSuccess)
{
    smap_debugfs_mod_exit();
}
