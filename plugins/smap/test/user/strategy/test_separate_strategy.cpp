/*
 * Description: smap5.0 user separate strategy ut code
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <unordered_set>
#include <vector>
#include "manage/manage.h"
#include "manage/node_matrix.h"
#include "strategy/strategy.h"
#include "strategy/separate_strategy.h"
#include "strategy/strategy_config.h"
#include "smap_user_log.h"

#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

static inline NumaBitmap BmFromWord(unsigned long w)
{
    NumaBitmap bm = {};
    (void)NumaBitmapAlloc(&bm);
    if (bm.bits != NULL) {
        bm.bits[0] = w;
    }
    return bm;
}

// Test-only compat: old compile-time NUMA constants (8 local + 18 remote)
#define OLD_REMOTE_NUMA_NUM 18
#define OLD_REMOTE_NUMA_BITS 18
#define OLD_LOCAL_NUMA_NUM 8
#define OLD_LOCAL_NUMA_BITS 8
#define OLD_MAX_NODES (OLD_LOCAL_NUMA_NUM + OLD_REMOTE_NUMA_NUM)

using namespace std;

/* cap=1024 时 mlist 全矩阵约 50MB，超出栈预算；gtest 用例串行执行，共用 BSS 数组 */
static struct MigList g_testMlist[SMAP_MAX_NUMNODES][SMAP_MAX_NUMNODES];

static struct MigList *TestMlist(void)
{
    (void)memset(g_testMlist, 0, sizeof(g_testMlist));
    return &g_testMlist[0][0];
}

class SeparateStrategyTest : public ::testing::Test {
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

TEST_F(SeparateStrategyTest, TestPeriodConfig)
{
    /* config file is RPM-installed at a fixed path; read must not crash with or without it */
    StrategyConfigRead();
}

extern "C" void FreeMlist(struct MigList *mlist);
TEST_F(SeparateStrategyTest, TestFreeMlist)
{
    struct MigList *mlist = TestMlist();
    NM_AT(mlist, 0, 0).addr = (uint64_t *)malloc(sizeof(uint64_t));
    FreeMlist(mlist);
    EXPECT_EQ(NULL, NM_AT(mlist, 0, 0).addr);
}

extern "C" int BuildSelectKMlistAddr(ProcessAttr *process, struct MigList *mlist,
    uint32_t numaOffset[SMAP_MAX_NUMNODES], int from, int to, SelectionMode mode);
void BuildActcData(int nid, const uint16_t data[], int size, ScanAttribute *scanAttribute)
{
    scanAttribute->actcLen[nid] = size;
    scanAttribute->actCount[nid].freqZero = 0;
    ActcData *actcData = (ActcData *)calloc(size, sizeof(ActcData));
    for (int i = 0; i < size; i++) {
        uint16_t freq = data[i];
        actcData[i].freq = freq;
        scanAttribute->actCount[nid].freqMax = std::max(scanAttribute->actCount[nid].freqMax, (actc_t)freq);
        scanAttribute->actCount[nid].freqMin = std::min(scanAttribute->actCount[nid].freqMin, (actc_t)freq);
        if (freq == 0) {
            scanAttribute->actCount[nid].freqZero++;
        } else {
            scanAttribute->actCount[nid].freqNum++;
        }
        scanAttribute->actCount[nid].freqSum += freq;
    }
    scanAttribute->actcData[nid] = actcData;
}

void initializeMigList(struct MigList *mlist)
{
    for (int i = 0; i < SMAP_MAX_NUMNODES; ++i) {
        for (int j = 0; j < SMAP_MAX_NUMNODES; ++j) {
            NM_AT(mlist, i, j).from = i;
            NM_AT(mlist, i, j).to = j;
        }
    }
}

extern "C" int PairMigrationStrategy(ProcessAttr *process, struct MigList *mlist);
static void BuildPairActcData(int nid, const uint16_t data[], int size, ScanAttribute *scanAttribute)
{
    BuildActcData(nid, data, size, scanAttribute);
    for (int i = 0; i < size; i++) {
        int bucket = std::min((int)data[i], FREQ_BUCKETS_SIZE - 1);
        scanAttribute->actCount[nid].freqBuckets[bucket]++;
    }
}

TEST_F(SeparateStrategyTest, TestPairMigrationStrategyBuilds2x2NetMigration)
{
    ProcessAttr process = {};
    struct MigList *mlist = TestMlist();
    const uint16_t local0[] = {1, 4, 2};
    const uint16_t local1[] = {0, 5};
    const uint16_t remote2[] = {3, 8, 7};
    const uint16_t remote3[] = {9, 6};
    /* actCount 为堆分配数组，须先初始化再由 BuildPairActcData 写入 */
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(2));
    ASSERT_EQ(0, InitProcessAttrArrays(&process));
    BuildPairActcData(0, local0, 3, &process.scanAttr);
    BuildPairActcData(1, local1, 2, &process.scanAttr);
    BuildPairActcData(2, remote2, 3, &process.scanAttr);
    BuildPairActcData(3, remote3, 2, &process.scanAttr);
    initializeMigList(mlist);
    NumaBitmapSet(&process.numaAttr.numaNodes, 2);
    NumaBitmapSet(&process.numaAttr.numaNodes, 3);

    NM_AT(process.strategyAttr.nrMigrateBackPages, 0, 2) = 2; /* promote: remote 2 -> local 0 */
    NM_AT(process.strategyAttr.nrMigratePages, 0, 3) = 2;
    NM_AT(process.strategyAttr.nrMigrateBackPages, 1, 2) = 1;
    NM_AT(process.strategyAttr.nrMigratePages, 1, 3) = 1;

    /* PairMigrationStrategy 按 for_each_remote_nid 遍历远端，需注册远端节点 2/3 */
    g_remoteNids[0] = 2;
    g_remoteNids[1] = 3;
    g_nrRemote = 2;
    ASSERT_EQ(0, PairMigrationStrategy(&process, mlist));
    g_nrRemote = 0;
    ASSERT_EQ(2U, NM_AT(mlist, 2, 0).nr);
    ASSERT_EQ(1U, NM_AT(mlist, 2, 1).nr);
    EXPECT_EQ(1U, NM_AT(mlist, 2, 0).addr[0]);
    EXPECT_EQ(2U, NM_AT(mlist, 2, 0).addr[1]);
    EXPECT_EQ(0U, NM_AT(mlist, 2, 1).addr[0]);
    ASSERT_EQ(2U, NM_AT(mlist, 0, 3).nr);
    ASSERT_EQ(1U, NM_AT(mlist, 1, 3).nr);
    EXPECT_EQ(0U, NM_AT(mlist, 0, 3).addr[0]);
    EXPECT_EQ(2U, NM_AT(mlist, 0, 3).addr[1]);
    EXPECT_EQ(0U, NM_AT(mlist, 1, 3).addr[0]);
    EXPECT_NE(NM_AT(mlist, 2, 0).addr[0], NM_AT(mlist, 2, 1).addr[0]);
    EXPECT_NE(NM_AT(mlist, 2, 0).addr[1], NM_AT(mlist, 2, 1).addr[0]);

    FreeMlist(mlist);
    for (int nid = 0; nid < 4; nid++) {
        free(process.scanAttr.actcData[nid]);
    }
    FreeProcessAttrArrays(&process);
}

TEST_F(SeparateStrategyTest, TestPairMigrationStrategyBuildsPairSwapAfterNetMigration)
{
    ProcessAttr process = {};
    struct MigList *mlist = TestMlist();
    const uint16_t local0[] = {0, 1, 6};
    const uint16_t remote2[] = {9, 8, 2};
    /* actCount 为堆分配数组，须先初始化再由 BuildPairActcData 写入 */
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(2));
    ASSERT_EQ(0, InitProcessAttrArrays(&process));
    BuildPairActcData(0, local0, 3, &process.scanAttr);
    BuildPairActcData(2, remote2, 3, &process.scanAttr);
    initializeMigList(mlist);
    NumaBitmapSet(&process.numaAttr.numaNodes, 2);
    NM_AT(process.strategyAttr.nrMigratePages, 0, 2) = 3;
    NM_AT(process.strategyAttr.nrMigrateBackPages, 0, 2) = 2;

    /* PairMigrationStrategy 按 for_each_remote_nid 遍历远端，需注册远端节点 2 */
    g_remoteNids[0] = 2;
    g_nrRemote = 1;
    ASSERT_EQ(0, PairMigrationStrategy(&process, mlist));
    g_nrRemote = 0;
    ASSERT_EQ(3U, NM_AT(mlist, 0, 2).nr);
    EXPECT_EQ(0U, NM_AT(mlist, 0, 2).addr[0]);
    EXPECT_EQ(1U, NM_AT(mlist, 0, 2).addr[1]);
    EXPECT_EQ(2U, NM_AT(mlist, 0, 2).addr[2]);
    ASSERT_EQ(2U, NM_AT(mlist, 2, 0).nr);
    EXPECT_EQ(0U, NM_AT(mlist, 2, 0).addr[0]);
    EXPECT_EQ(1U, NM_AT(mlist, 2, 0).addr[1]);

    FreeMlist(mlist);
    free(process.scanAttr.actcData[0]);
    free(process.scanAttr.actcData[2]);
    FreeProcessAttrArrays(&process);
}

TEST_F(SeparateStrategyTest, TestPairMigrationStrategyUbBwSwapStopStripsSwap)
{
    ProcessAttr process = {};
    struct MigList *mlist = TestMlist();
    const uint16_t local0[] = {0, 1, 6};
    const uint16_t remote2[] = {9, 8, 2};
    /* actCount 为堆分配数组，须先初始化再由 BuildPairActcData 写入 */
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(2));
    ASSERT_EQ(0, InitProcessAttrArrays(&process));
    BuildPairActcData(0, local0, 3, &process.scanAttr);
    BuildPairActcData(2, remote2, 3, &process.scanAttr);
    initializeMigList(mlist);
    NumaBitmapSet(&process.numaAttr.numaNodes, 2);
    NM_AT(process.strategyAttr.nrMigratePages, 0, 2) = 3; /* local -> remote */
    NM_AT(process.strategyAttr.nrMigrateBackPages, 0, 2) = 2; /* remote -> local */
    process.strategyAttr.ubBwRestrict[2] = UB_BW_SWAP_STOP;

    /* PairMigrationStrategy 按 for_each_remote_nid 遍历远端，需注册远端节点 2 */
    g_remoteNids[0] = 2;
    g_nrRemote = 1;
    ASSERT_EQ(0, PairMigrationStrategy(&process, mlist));
    g_nrRemote = 0;

    /* swapPages = MIN(3, 2) = 2 -> 0; demotePages = 3 - 2 = 1, promotePages = 2 - 2 = 0.
     * UB_BW_SWAP_STOP only strips swap, net demote/promote stays. */
    ASSERT_EQ(1U, NM_AT(mlist, 0, 2).nr);
    EXPECT_EQ(0U, NM_AT(mlist, 0, 2).addr[0]);
    ASSERT_EQ(0U, NM_AT(mlist, 2, 0).nr);
    EXPECT_EQ(nullptr, NM_AT(mlist, 2, 0).addr);

    FreeMlist(mlist);
    free(process.scanAttr.actcData[0]);
    free(process.scanAttr.actcData[2]);
    FreeProcessAttrArrays(&process);
}

TEST_F(SeparateStrategyTest, TestPairMigrationStrategyDisabledRemoteSkipsBothDirections)
{
    ProcessAttr process = {};
    struct MigList *mlist = TestMlist();
    const uint16_t remote2[] = {5};
    /* actCount 为堆分配数组，须先初始化再由 BuildPairActcData 写入 */
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(2));
    ASSERT_EQ(0, InitProcessAttrArrays(&process));
    BuildPairActcData(2, remote2, 1, &process.scanAttr);
    initializeMigList(mlist);
    NumaBitmapSet(&process.numaAttr.numaNodes, 2);
    NM_AT(process.strategyAttr.nrMigratePages, 0, 2) = 1;
    NM_AT(process.strategyAttr.nrMigrateBackPages, 0, 2) = 1;
    EnvAtomicSet(&g_forbiddenNodes[2], 1);

    int ret = PairMigrationStrategy(&process, mlist);
    EnvAtomicSet(&g_forbiddenNodes[2], 0);
    ASSERT_EQ(0, ret);
    EXPECT_EQ(0U, NM_AT(mlist, 0, 2).nr);
    EXPECT_EQ(0U, NM_AT(mlist, 2, 0).nr);

    FreeMlist(mlist);
    free(process.scanAttr.actcData[2]);
    FreeProcessAttrArrays(&process);
}

TEST_F(SeparateStrategyTest, TestPairMigrationStrategySkipsUnmanagedRemote)
{
    ProcessAttr process = {};
    struct MigList *mlist = TestMlist();
    const uint16_t remote2[] = {5};
    /* actCount 为堆分配数组，须先初始化再由 BuildPairActcData 写入 */
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(2));
    ASSERT_EQ(0, InitProcessAttrArrays(&process));
    BuildPairActcData(2, remote2, 1, &process.scanAttr);
    initializeMigList(mlist);
    /* 远端已被 remove 出管理范围（numaNodes 位图已清），但方向矩阵仍残留本轮计划 */
    NM_AT(process.strategyAttr.nrMigratePages, 0, 2) = 1;
    NM_AT(process.strategyAttr.nrMigrateBackPages, 0, 2) = 1;
    g_remoteNids[0] = 2;
    g_nrRemote = 1;

    ASSERT_EQ(0, PairMigrationStrategy(&process, mlist));
    g_nrRemote = 0;
    EXPECT_EQ(0U, NM_AT(mlist, 0, 2).nr);
    EXPECT_EQ(0U, NM_AT(mlist, 2, 0).nr);

    FreeMlist(mlist);
    free(process.scanAttr.actcData[2]);
    FreeProcessAttrArrays(&process);
}
