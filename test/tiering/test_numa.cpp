/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: SMAP3.0 attr模块测试代码
 */
#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/numa.h>
#include <linux/mm.h>
#include <linux/list.h>

#include "common.h"
#include "numa.h"

using namespace std;

class NumaTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

extern "C" int setup_node_data(int nid, u64 start_pfn, u64 end_pfn);
extern "C" void teardown_node_data(int nid);
extern "C" bool is_smap_pg_huge(void);
TEST_F(NumaTest, GetNodeNrFreePages_NullNode)
{
    /* nid with no node_data setup returns 0 */
    unsigned long ret = get_node_nr_free_pages(MAX_NUMNODES - 1);
    EXPECT_EQ(0UL, ret);
}

TEST_F(NumaTest, GetNodeNrFreePages)
{
    int ret;
    int nid = 0;
    int tmp_ret;
    int numa0_id = 0;
    u64 numa0_sp = 5;
    u64 numa0_ep = 1029;

    tmp_ret = setup_node_data(numa0_id, numa0_sp, numa0_ep);
    ASSERT_EQ(0, tmp_ret);
    MOCKER(zone_page_state).stubs().will(returnValue(0));
    ret = get_node_nr_free_pages(nid);
    EXPECT_EQ(0, ret);
    teardown_node_data(numa0_id);
}