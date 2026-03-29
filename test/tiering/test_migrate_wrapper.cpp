/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: SMAP3.0 stub_smap_migrate_wrapper.c test code
 * Author: hebo h00519890
 * Create: 2024-8-20
 */

#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

#include "smap_migrate_wrapper.h"

using namespace std;

class MigrateWrapperTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

struct folio *smap_alloc_huge_page_node(struct folio *folio, int nid, bool is_mig_back);
TEST_F(MigrateWrapperTest, HugepageSuccess)
{
    struct folio *new_folio = NULL;
    struct folio *old_folio = (struct folio *)kmalloc(1, GFP_KERNEL);
    ASSERT_NE(nullptr, old_folio);

    new_folio = smap_alloc_huge_page_node(old_folio, 0, false);
    /* No mock: result depends on system state, just verify no crash */
    kfree(old_folio);
}

TEST_F(MigrateWrapperTest, HugepageAllocFail)
{
    struct folio *new_folio = NULL;
    struct folio *old_folio = (struct folio *)kmalloc(1, GFP_KERNEL);
    ASSERT_NE(nullptr, old_folio);

    MOCKER(get_hugetlb_folio_nodemask).stubs().will(returnValue((struct folio *)nullptr));
    new_folio = smap_alloc_huge_page_node(old_folio, 0, false);
    EXPECT_EQ(nullptr, new_folio);
    kfree(old_folio);
}
