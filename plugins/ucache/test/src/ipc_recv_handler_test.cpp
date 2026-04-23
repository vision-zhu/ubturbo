/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

#define private public
#include "resource_collector.h"
#include "task_executor.h"
#include "turbo_def.h"
#include "turbo_strategy_executor.h"

#define MOCKER_CPP(api, TT) MOCKCPP_NS::mockAPI(#api, reinterpret_cast<TT>(api))

using namespace std;
using namespace turbo::ucache;

class IPCRecvHandlerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        cout << "[phase SetUp Begin]" << endl;
        cout << "[phase SetUp End]" << endl;
    }
    void TearDown() override
    {
        cout << "[phase TearDown Begin]" << endl;
        GlobalMockObject::verify();
        cout << "[phase TearDown End]" << endl;
    }
};

TEST_F(IPCRecvHandlerTest, Init_TEST)
{
    TaskExecutor::Init();
}