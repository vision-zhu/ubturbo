/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: iomem test
 */
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <iostream>
#include <cstring>
#include "gtest/gtest.h"
#include "linux/compiler_attributes.h"
#include "linux/device.h"
#include "linux/memory.h"
#include "linux/notifier.h"
#include "linux/ioport.h"
#include "linux/mm_types.h"
#include "linux/types.h"
#include "linux/dmaengine.h"
#include "linux/migrate.h"
#include "mockcpp/ChainingMockHelper.h"
#include "mockcpp/mokc.h"
#include "stub_struct.h"
#include "urma.h"
#include "ubcore_types.h"
#include "ubcore_opcode.h"

using namespace std;

class TestUbDmaIomem : public ::testing::Test {
protected:
    void SetUp() override
    {
        cout << "[Phase SetUp Begin]" << endl;
        cout << "[Phase SetUp End]" << endl;
    }
    void TearDown() override
    {
        cout << "[Phase TearDown Begin]" << endl;
        GlobalMockObject::verify();
        cout << "[Phase TearDown End]" << endl;
    }
};

extern "C" {
    int insert_remote_ram(u64 pa_start, u64 pa_end, struct list_head *head);
    int pfn_valid(unsigned long pfn);
    int page_to_nid(const struct page *page);
    int update_resource(struct resource *r, void *arg);
}

TEST_F(TestUbDmaIomem, insert_remote_ram_test)
{
    list_head head;
    MOCKER(pfn_valid).stubs().will(returnValue(0));
    int ret = insert_remote_ram(0, 1, &head);
    EXPECT_EQ(ret, 0);

    GlobalMockObject::verify();
    MOCKER(kmalloc).stubs().will(returnValue((void *)nullptr));
    ret = insert_remote_ram(0, 1, &head);
    EXPECT_EQ(ret, -ENOMEM);

    GlobalMockObject::verify();
    ret = insert_remote_ram(0, 1, &head);
    EXPECT_EQ(ret, 0);

    GlobalMockObject::verify();
    MOCKER(page_to_nid).stubs().will(returnValue(-1));
    ret = insert_remote_ram(0, 1, &head);
    EXPECT_EQ(ret, -EINVAL);
}

TEST_F(TestUbDmaIomem, update_resource_test)
{
    list_head head;
    resource rsc;
    rsc.flags = IORESOURCE_SYSRAM_DRIVER_MANAGED;
    MOCKER(pfn_valid).stubs().will(returnValue(0));
    int ret = update_resource(nullptr, nullptr);
    EXPECT_EQ(ret, -EINVAL);

    MOCKER(insert_remote_ram).stubs().will(returnValue(1));
    ret = update_resource(&rsc, &head);
    EXPECT_EQ(ret, 1);

    GlobalMockObject::verify();
    MOCKER(insert_remote_ram).stubs().will(returnValue(0));
    ret = update_resource(&rsc, &head);
    EXPECT_EQ(ret, 0);
}