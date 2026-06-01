/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: smap5.0 user allocation ut code
 */

#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

#include "manage/manage.h"
#include "strategy/separate_strategy.h"
#include "strategy/strategy.h"
#include "smap_user_log.h"

using namespace std;

class StrategyTest : public ::testing::Test {
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

extern "C" int RunStrategy(ProcessAttr *process, struct MigList mlist[MAX_NODES][MAX_NODES], size_t mlistSize);
TEST_F(StrategyTest, TestRunStrategyOne)
{
    size_t mlistSize = NR_LEVEL;
    ProcessAttr process;
    ActcData actcData = { 0 };
    process.scanAttr.actcData[0] = &actcData;
    struct MigList mlist[MAX_NODES][MAX_NODES] = {};
    MOCKER(IsHugeMode).stubs().will(returnValue(true));
    MOCKER(SeparateStrategy).stubs().will(returnValue(0));
    MOCKER(SeparateStrategy4K).stubs().will(returnValue(0));
    int ret = RunStrategy(&process, mlist, mlistSize);
    EXPECT_EQ(-EINVAL, ret);
}

TEST_F(StrategyTest, TestRunStrategyTwo)
{
    size_t mlistSize = NR_LEVEL;
    ProcessAttr process;
    ActcData actcData = { 0 };
    process.scanAttr.actcData[0] = &actcData;
    struct MigList mlist[MAX_NODES][MAX_NODES] = {};
    MOCKER(IsHugeMode).stubs().will(returnValue(false));
    MOCKER(SeparateStrategy).stubs().will(returnValue(0));
    MOCKER(SeparateStrategy4K).stubs().will(returnValue(0));
    int ret = RunStrategy(&process, mlist, mlistSize);
    EXPECT_EQ(-EINVAL, ret);
}

TEST_F(StrategyTest, TestRunStrategyThree)
{
    size_t mlistSize = 0;
    ProcessAttr process;
    struct MigList mlist[MAX_NODES][MAX_NODES] = {};
    MOCKER(SeparateStrategy).stubs().will(returnValue(0));
    MOCKER(SeparateStrategy4K).stubs().will(returnValue(0));
    int ret = RunStrategy(&process, mlist, mlistSize);
    EXPECT_EQ(-EINVAL, ret);
}

extern "C" int snprintf_s(char *strDest, unsigned long destMax, unsigned long count, const char *format, ...);
extern "C" FILE *fopen(const char *__restrict __filename, const char *__restrict __modes);
extern "C" char *fgets(char *__restrict __s, int __n, FILE *__restrict __stream);
extern "C" int fclose(FILE *__stream);
extern "C" int sscanf_s(const char *buffer, const char *format, ...);
TEST_F(StrategyTest, TestGetNrFreePagesByNode)
{
    int nid = 1;
    int ret;
    MOCKER((int (*)(char *, unsigned long, unsigned long, char const *, void *))snprintf_s)
        .stubs()
        .will(returnValue(-1));
    ret = GetNrFreePagesByNode(nid);
    EXPECT_EQ(0, ret);
}

TEST_F(StrategyTest, TestGetNrFreePagesByNodeTwo)
{
    int nid = 1;
    int ret;
    MOCKER((int (*)(char *, unsigned long, unsigned long, char const *, void *))snprintf_s)
        .stubs()
        .will(returnValue(0));
    MOCKER(fopen).stubs().will(returnValue(static_cast<FILE *>(nullptr)));
    ret = GetNrFreePagesByNode(nid);
    EXPECT_EQ(0, ret);
}

TEST_F(StrategyTest, TestGetNrFreePagesByNodeThree)
{
    int nid = 1;
    int ret;
    MOCKER((int (*)(char *, unsigned long, unsigned long, char const *, void *))snprintf_s)
        .stubs()
        .will(returnValue(0));
    static FILE fake_file;
    MOCKER(fopen).stubs().will(returnValue(&fake_file));
    MOCKER(fgets).stubs().will(returnValue(static_cast<char *>(nullptr)));
    MOCKER(fclose).stubs().will(returnValue(0));
    ret = GetNrFreePagesByNode(nid);
    EXPECT_EQ(0, ret);
}

TEST_F(StrategyTest, TestGetNrFreePagesByNodeFour)
{
    int nid = 1;
    int ret;
    MOCKER((int (*)(char *, unsigned long, unsigned long, char const *, void *))snprintf_s)
        .stubs()
        .will(returnValue(0));
    static FILE fake_file;
    MOCKER(fopen).stubs().will(returnValue(&fake_file));
    MOCKER(fgets).stubs().will(returnValue(static_cast<char *>("1"))).then(returnValue((static_cast<char *>(nullptr))));
    MOCKER(fclose).stubs().will(returnValue(-1));
    ret = GetNrFreePagesByNode(nid);
    EXPECT_EQ(0, ret);
}

TEST_F(StrategyTest, TestGetNrFreeHugePagesByNode)
{
    int nid = 1;
    int ret;
    MOCKER((int (*)(char *, unsigned long, unsigned long, char const *, void *))snprintf_s)
        .stubs()
        .will(returnValue(-1));
    ret = GetNrFreeHugePagesByNode(nid);
    EXPECT_EQ(0, ret);
}

TEST_F(StrategyTest, TestGetNrFreeHugePagesByNodeTwo)
{
    int nid = 1;
    int ret;
    MOCKER((int (*)(char *, unsigned long, unsigned long, char const *, void *))snprintf_s)
        .stubs()
        .will(returnValue(0));
    MOCKER(fopen).stubs().will(returnValue(static_cast<FILE *>(nullptr)));
    ret = GetNrFreeHugePagesByNode(nid);
    EXPECT_EQ(0, ret);
}

TEST_F(StrategyTest, TestGetNrFreeHugePagesByNodeThree)
{
    int nid = 1;
    int ret;
    MOCKER((int (*)(char *, unsigned long, unsigned long, char const *, void *))snprintf_s)
        .stubs()
        .will(returnValue(0));
    static FILE fake_file;
    MOCKER(fopen).stubs().will(returnValue(&fake_file));
    MOCKER(fgets).stubs().will(returnValue(static_cast<char *>(nullptr)));
    MOCKER(fclose).stubs().will(returnValue(0));
    ret = GetNrFreeHugePagesByNode(nid);
    EXPECT_EQ(0, ret);
}

TEST_F(StrategyTest, TestGetNrFreeHugePagesByNodeFour)
{
    int nid = 1;
    int ret;
    static FILE fake_file;
    MOCKER((int (*)(char *, unsigned long, unsigned long, char const *, void *))snprintf_s)
        .stubs()
        .will(returnValue(0));
    MOCKER(fopen).stubs().will(returnValue(&fake_file));
    MOCKER(fgets).stubs().will(returnValue(static_cast<char *>("1")));
    MOCKER((int (*)(char const *, char const *, void *))sscanf_s)
        .stubs()
        .will(returnValue(0));
    MOCKER(fclose).stubs().will(returnValue(-1));
    ret = GetNrFreeHugePagesByNode(nid);
    EXPECT_EQ(0, ret);
}

extern "C" RunMode GetRunMode(void);
extern "C" struct ProcessManager *GetProcessManager(void);
extern "C" int GetNrLocalNuma(void);
extern "C" bool IsRemoteNidValid(int nid);
extern "C" void EnvMutexLock(EnvMutex *mutex);
extern "C" void EnvMutexUnlock(EnvMutex *mutex);

TEST_F(StrategyTest, TestCheckAndLimitMigrationByRemoteAvailable_WaterlineMode)
{
    size_t mlistSize = MAX_NODES;
    ProcessAttr process;
    memset(&process, 0, sizeof(ProcessAttr));
    ActcData actcData = { 0 };
    process.scanAttr.actcData[0] = &actcData;
    process.scanAttr.actcData[4] = &actcData;
    process.scanAttr.actcLen[0] = 100;
    process.scanAttr.actcLen[4] = 100;
    process.numaAttr.numaNodes = 0x1 | (0x1 << LOCAL_NUMA_BITS);
    process.migrateMode = MIG_MEMSIZE_MODE;
    process.strategyAttr.nrMigratePages[0][4] = 1000;
    struct MigList mlist[MAX_NODES][MAX_NODES] = {};
    static ProcessManager manager;
    memset(&manager, 0, sizeof(ProcessManager));
    manager.nrLocalNuma = 4;
    manager.remoteNumaInfo.usedInfo[0].size = 500;
    manager.remoteNumaInfo.usedInfo[0].used = 100;
    MOCKER(GetRunMode).stubs().will(returnValue(WATERLINE_MODE));
    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    MOCKER(IsRemoteNidValid).stubs().will(returnValue(true));
    MOCKER(EnvMutexLock).stubs();
    MOCKER(EnvMutexUnlock).stubs();
    MOCKER(IsHugeMode).stubs().will(returnValue(true));
    MOCKER(SeparateStrategy).stubs().will(returnValue(0));
    MOCKER(GetNrFreeHugePagesByNode).stubs().will(returnValue(1000));
    int ret = RunStrategy(&process, mlist, mlistSize);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(400u, process.strategyAttr.nrMigratePages[0][4]);
}

TEST_F(StrategyTest, TestCheckAndLimitMigrationByRemoteAvailable_MemPoolMode)
{
    size_t mlistSize = MAX_NODES;
    ProcessAttr process;
    memset(&process, 0, sizeof(ProcessAttr));
    ActcData actcData = { 0 };
    process.scanAttr.actcData[0] = &actcData;
    process.scanAttr.actcData[4] = &actcData;
    process.scanAttr.actcLen[0] = 100;
    process.scanAttr.actcLen[4] = 100;
    process.numaAttr.numaNodes = 0x1 | (0x1 << LOCAL_NUMA_BITS);
    process.migrateMode = MIG_MEMSIZE_MODE;
    process.strategyAttr.nrMigratePages[0][4] = 1000;
    struct MigList mlist[MAX_NODES][MAX_NODES] = {};
    MOCKER(GetRunMode).stubs().will(returnValue(MEM_POOL_MODE));
    MOCKER(IsHugeMode).stubs().will(returnValue(true));
    MOCKER(SeparateStrategy).stubs().will(returnValue(0));
    MOCKER(GetNrFreeHugePagesByNode).stubs().will(returnValue(1000));
    int ret = RunStrategy(&process, mlist, mlistSize);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(1000u, process.strategyAttr.nrMigratePages[0][4]);
}

TEST_F(StrategyTest, TestCheckAndLimitMigrationByRemoteAvailable_RatioMode)
{
    size_t mlistSize = MAX_NODES;
    ProcessAttr process;
    memset(&process, 0, sizeof(ProcessAttr));
    ActcData actcData = { 0 };
    process.scanAttr.actcData[0] = &actcData;
    process.scanAttr.actcData[4] = &actcData;
    process.scanAttr.actcLen[0] = 100;
    process.scanAttr.actcLen[4] = 100;
    process.numaAttr.numaNodes = 0x1 | (0x1 << LOCAL_NUMA_BITS);
    process.migrateMode = MIG_RATIO_MODE;
    process.strategyAttr.nrMigratePages[0][4] = 1000;
    struct MigList mlist[MAX_NODES][MAX_NODES] = {};
    MOCKER(GetRunMode).stubs().will(returnValue(WATERLINE_MODE));
    MOCKER(IsHugeMode).stubs().will(returnValue(true));
    MOCKER(SeparateStrategy).stubs().will(returnValue(0));
    MOCKER(GetNrFreeHugePagesByNode).stubs().will(returnValue(1000));
    int ret = RunStrategy(&process, mlist, mlistSize);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(1000u, process.strategyAttr.nrMigratePages[0][4]);
}

TEST_F(StrategyTest, TestCheckAndLimitMigrationByRemoteAvailable_ZeroAvailable)
{
    size_t mlistSize = MAX_NODES;
    ProcessAttr process;
    memset(&process, 0, sizeof(ProcessAttr));
    ActcData actcData = { 0 };
    process.scanAttr.actcData[0] = &actcData;
    process.scanAttr.actcData[4] = &actcData;
    process.scanAttr.actcLen[0] = 100;
    process.scanAttr.actcLen[4] = 100;
    process.numaAttr.numaNodes = 0x1 | (0x1 << LOCAL_NUMA_BITS);
    process.migrateMode = MIG_MEMSIZE_MODE;
    process.strategyAttr.nrMigratePages[0][4] = 1000;
    struct MigList mlist[MAX_NODES][MAX_NODES] = {};
    static ProcessManager manager;
    memset(&manager, 0, sizeof(ProcessManager));
    manager.nrLocalNuma = 4;
    manager.remoteNumaInfo.usedInfo[0].size = 100;
    manager.remoteNumaInfo.usedInfo[0].used = 100;
    MOCKER(GetRunMode).stubs().will(returnValue(WATERLINE_MODE));
    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    MOCKER(IsRemoteNidValid).stubs().will(returnValue(true));
    MOCKER(EnvMutexLock).stubs();
    MOCKER(EnvMutexUnlock).stubs();
    MOCKER(IsHugeMode).stubs().will(returnValue(true));
    MOCKER(SeparateStrategy).stubs().will(returnValue(0));
    MOCKER(GetNrFreeHugePagesByNode).stubs().will(returnValue(1000));
    int ret = RunStrategy(&process, mlist, mlistSize);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0u, process.strategyAttr.nrMigratePages[0][4]);
}
