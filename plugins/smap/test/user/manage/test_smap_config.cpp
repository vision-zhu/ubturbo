/*
 * Description: smap smap config ut code
 * Create: 2025-04-06
 */

#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

#include "securec.h"
#include "manage/manage.h"
#include "manage/node_matrix.h"
#include "advanced-strategy/scene.h"
#include "manage/smap_config.h"

using namespace std;

// Test-only compat: old compile-time NUMA constants (8 local + 18 remote)
#define OLD_REMOTE_NUMA_NUM 18
#define OLD_REMOTE_NUMA_BITS 18
#define OLD_LOCAL_NUMA_NUM 8
#define OLD_LOCAL_NUMA_BITS 8
#define OLD_MAX_NODES (OLD_LOCAL_NUMA_NUM + OLD_REMOTE_NUMA_NUM)

static ProcessAttr *PmHeadAttr(struct ProcessManager *pm)
{
    for (int i = 0; i < MAX_PID_SLOTS; i++) {
        if (pm->slots[i].attr != nullptr) {
            return pm->slots[i].attr;
        }
    }
    return nullptr;
}

static ProcessAttr *PmNthAttr(struct ProcessManager *pm, int n)
{
    int count = 0;
    for (int i = 0; i < MAX_PID_SLOTS; i++) {
        if (pm->slots[i].attr != nullptr) {
            if (count == n) {
                return pm->slots[i].attr;
            }
            count++;
        }
    }
    return nullptr;
}

/* 按运行期 cap 构造一条 V3 序列化记录（定长头 + 变长位图 + 目标数组），返回记录字节数 */
static size_t BuildV3Record(char *buf, pid_t pid, uint8_t scanType, uint8_t type, uint8_t state,
                            uint8_t migrateMode, uint32_t scanTime, const int *bits, int nrBits,
                            const struct PersistRemoteTarget *targets, int nrTargets)
{
    uint32_t nrWords = NumaWordsForCap();
    struct ProcessPayloadHeader *h = reinterpret_cast<struct ProcessPayloadHeader *>(buf);
    h->pid = pid;
    h->scanType = scanType;
    h->type = type;
    h->state = state;
    h->migrateMode = migrateMode;
    h->srcNid = NUMA_NO_NODE;
    h->scanTime = scanTime;
    h->duration = 0;
    h->nrWords = nrWords;
    h->count = nrTargets;
    h->ignoreRemoteCapacity = 0;

    unsigned long *wordBuf = reinterpret_cast<unsigned long *>(buf + CONFIG_PROC_HEADER_LEN);
    for (int i = 0; i < nrBits; i++) {
        wordBuf[NID_WORD(bits[i])] |= NID_MASK(bits[i]);
    }
    struct PersistRemoteTarget *t = reinterpret_cast<struct PersistRemoteTarget *>(wordBuf + nrWords);
    for (int i = 0; i < nrTargets; i++) {
        t[i] = targets[i];
    }
    return CONFIG_PROC_HEADER_LEN + (size_t)nrWords * sizeof(unsigned long) +
           (size_t)nrTargets * sizeof(struct PersistRemoteTarget);
}

class SmapConfigTest : public ::testing::Test {
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

extern "C" size_t CalcNumaPayloadNum(void);
TEST_F(SmapConfigTest, TestCalcNumaPayloadNum)
{
    int ret;
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(2));
    ret = CalcNumaPayloadNum();
    EXPECT_EQ(2 * SMAP_MAX_NUMNODES + SMAP_MAX_NUMNODES, ret);
}

extern "C" size_t CalcNumaPayloadLen(void);
TEST_F(SmapConfigTest, TestCalcNumaPayloadLen)
{
    int ret;
    int nr = 2;
    MOCKER(CalcNumaPayloadNum).stubs().will(returnValue(nr));
    ret = CalcNumaPayloadLen();
    EXPECT_EQ(nr * CONFIG_NUMA_LEN, ret);
}

extern "C" size_t CalcNumaConfigLen(void);
TEST_F(SmapConfigTest, TestCalcNumaConfigLen)
{
    int ret;
    int len = 10;
    MOCKER(CalcNumaPayloadLen).stubs().will(returnValue(len));
    ret = CalcNumaConfigLen();
    EXPECT_EQ(len + PAYLOAD_HEADER_LEN, ret);
}

extern "C" size_t CalcProcessConfigLen(size_t payloadBytes);
TEST_F(SmapConfigTest, TestCalcProcessConfigLen)
{
    size_t ret;
    size_t payloadBytes = 64;

    ret = CalcProcessConfigLen(0);
    EXPECT_EQ(PAYLOAD_HEADER_LEN, ret);

    ret = CalcProcessConfigLen(payloadBytes);
    EXPECT_EQ(PAYLOAD_HEADER_LEN + payloadBytes, ret);
}

extern "C" size_t GetProcessConfigLen(struct PayloadHeader *header);
TEST_F(SmapConfigTest, TestGetProcessConfigLen)
{
    int ret;
    struct PayloadHeader header = {.len = 20};

    ret = GetProcessConfigLen(&header);
    EXPECT_EQ(header.len + PAYLOAD_HEADER_LEN, ret);
}

extern "C" size_t CalcConfigLen(size_t processPayloadBytes);
TEST_F(SmapConfigTest, TestCalcConfigLen)
{
    int ret;
    int nrProcess = 2;
    int numaConfigLen = 20;
    int processConfigLen = 30;

    MOCKER(CalcNumaConfigLen).stubs().will(returnValue(numaConfigLen));
    MOCKER(CalcProcessConfigLen).stubs().will(returnValue(processConfigLen));
    ret = CalcConfigLen(nrProcess);
    EXPECT_EQ(CONFIG_HEADER_LEN + numaConfigLen + processConfigLen, ret);
}

extern "C" char *JumpToNumaConfig(char *base);
TEST_F(SmapConfigTest, TestJumpToNumaConfig)
{
    char addr;
    char *ret;

    ret = JumpToNumaConfig(&addr);
    EXPECT_EQ(&addr + CONFIG_HEADER_LEN, ret);
}

extern "C" char *JumpToNumaPayload(char *numaBase);
TEST_F(SmapConfigTest, TestJumpToNumaPayload)
{
    char addr;
    char *ret;

    ret = JumpToNumaPayload(&addr);
    EXPECT_EQ(&addr + PAYLOAD_HEADER_LEN, ret);
}

extern "C" char *JumpToProcessConfig(char *base);
TEST_F(SmapConfigTest, TestJumpToProcessConfig)
{
    size_t numaConfigLen = 20;
    char addr;
    char *ret;

    MOCKER(JumpToNumaConfig).stubs().will(returnValue(&addr));
    MOCKER(CalcNumaConfigLen).stubs().will(returnValue(numaConfigLen));
    ret = JumpToProcessConfig(&addr);
    EXPECT_EQ(&addr + numaConfigLen, ret);
}

extern "C" char *JumpToProcessPayload(char *processBase);
TEST_F(SmapConfigTest, TestJumpToProcessPayload)
{
    char addr;
    char *ret;

    ret = JumpToProcessPayload(&addr);
    EXPECT_EQ(&addr + PAYLOAD_HEADER_LEN, ret);
}

extern "C" void WriteRunMode(char *addr, RunMode mode);
TEST_F(SmapConfigTest, TestWriteRunMode)
{
    struct SmapConfigHeader header = {.runMode = WATERLINE_MODE};
    RunMode runMode = MEM_POOL_MODE;
    WriteRunMode((char *)&header, runMode);
    EXPECT_EQ(header.runMode, runMode);
}

extern "C" void WriteHeader(char *addr, RunMode mode, size_t len);
TEST_F(SmapConfigTest, TestWriteHeader)
{
    int ret;
    RunMode runMode = MEM_POOL_MODE;
    size_t len = 20;
    struct SmapConfigHeader header = {0};

    ASSERT_EQ(0, header.ver);
    ASSERT_EQ(0, header.headerLen);
    ASSERT_EQ(0, header.totalLen);
    WriteHeader((char *)&header, runMode, len);
    EXPECT_EQ(SMAP_CONFIG_VER, header.ver);
    EXPECT_EQ(runMode, header.runMode);
    EXPECT_EQ(CONFIG_HEADER_LEN, header.headerLen);
    EXPECT_EQ(len, header.totalLen);
}

extern "C" char *JumpToNumaPayload(char *numaBase);
extern "C" void WriteNumaConfig(char *base);
TEST_F(SmapConfigTest, TestWriteNumaConfig)
{
    int ret;
    int nrLocal = 4;
    struct PayloadHeader header;
    struct ProcessManager manager; memset(&manager, 0, sizeof(manager));
    manager.remoteNumaInfo.privateSize =
        (uint64_t *)calloc(NM_COUNT(nrLocal), sizeof(uint64_t));
    manager.remoteNumaInfo.privateUsedInfo =
        (struct RemoteNumaUsedInfo *)calloc(NM_COUNT(nrLocal), sizeof(struct RemoteNumaUsedInfo));
    ASSERT_NE(nullptr, manager.remoteNumaInfo.privateSize);
    ASSERT_NE(nullptr, manager.remoteNumaInfo.privateUsedInfo);
    NM_AT(manager.remoteNumaInfo.privateSize, 0, 0) = 128;
    struct NumaPayload *payload =
        (struct NumaPayload *)calloc((nrLocal + 1) * SMAP_MAX_NUMNODES, sizeof(*payload));

    ASSERT_NE(nullptr, payload);

    MOCKER(GetNrLocalNuma).stubs().will(returnValue(nrLocal));
    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(JumpToNumaPayload).stubs().will(returnValue(reinterpret_cast<char *>(payload)));
    WriteNumaConfig((char *)&header);
    EXPECT_EQ(128, payload[0].size);
    free(payload);
    free(manager.remoteNumaInfo.privateSize);
    free(manager.remoteNumaInfo.privateUsedInfo);
    manager.remoteNumaInfo.privateSize = nullptr;
    manager.remoteNumaInfo.privateUsedInfo = nullptr;
}

extern "C" void WriteEmptyProcessConfigs(char *base);
extern "C" bool IsConfigHeaderValid(struct SmapConfigHeader *header);
TEST_F(SmapConfigTest, TestIsConfigHeaderValid)
{
    bool ret;
    size_t numaConfigLen = 20;
    size_t minLen = CONFIG_HEADER_LEN + numaConfigLen;
    struct SmapConfigHeader header = {.ver = SMAP_CONFIG_VER, .headerLen = CONFIG_HEADER_LEN, .totalLen = minLen};

    MOCKER(CalcNumaConfigLen).stubs().will(returnValue(numaConfigLen));
    ret = IsConfigHeaderValid(&header);
    EXPECT_TRUE(ret);

    header.totalLen--;
    ret = IsConfigHeaderValid(&header);
    EXPECT_FALSE(ret);
}

TEST_F(SmapConfigTest, TestIsConfigHeaderInValid)
{
    bool ret;
    struct SmapConfigHeader header = {.ver = 0};
    ret = IsConfigHeaderValid(&header);
    EXPECT_FALSE(ret);
}

extern "C" bool IsRunModeValid(RunMode runMode);
TEST_F(SmapConfigTest, TestIsConfigHeaderValidTwo)
{
    bool ret;
    struct SmapConfigHeader header = {.ver = SMAP_CONFIG_VER};
    MOCKER(IsRunModeValid).stubs().will(returnValue(false));
    ret = IsConfigHeaderValid(&header);
    EXPECT_FALSE(ret);

    GlobalMockObject::verify();
    MOCKER(IsRunModeValid).stubs().will(returnValue(true));
    header.headerLen = CONFIG_HEADER_LEN;
    ret = IsConfigHeaderValid(&header);
    EXPECT_FALSE(ret);
}

extern "C" void ReadHeader(char *addr, struct SmapConfigHeader *header);
TEST_F(SmapConfigTest, TestReadHeader)
{
    struct SmapConfigHeader src = {
        .ver = SMAP_CONFIG_VER, .runMode = MEM_POOL_MODE, .headerLen = CONFIG_HEADER_LEN, .totalLen = 30};
    struct SmapConfigHeader dest = {0};

    ReadHeader((char *)&src, &dest);
    EXPECT_EQ(src.ver, dest.ver);
    EXPECT_EQ(src.runMode, dest.runMode);
    EXPECT_EQ(src.headerLen, dest.headerLen);
    EXPECT_EQ(src.totalLen, dest.totalLen);
}

extern "C" bool IsRunModeValid(RunMode runMode);
TEST_F(SmapConfigTest, TestIsRunModeValidErr)
{
    int ret;
    int runMode = -1;

    ret = IsRunModeValid((RunMode)runMode);
    EXPECT_FALSE(ret);

    runMode = MAX_RUN_MODE;
    ret = IsRunModeValid((RunMode)runMode);
    EXPECT_FALSE(ret);
}

TEST_F(SmapConfigTest, TestIsRunModeValidNormal)
{
    int ret;
    int runMode = WATERLINE_MODE;

    ret = IsRunModeValid((RunMode)runMode);
    EXPECT_TRUE(ret);

    runMode = MEM_POOL_MODE;
    ret = IsRunModeValid((RunMode)runMode);
    EXPECT_TRUE(ret);
}

extern "C" void RecoverRunMode(RunMode runMode);
TEST_F(SmapConfigTest, TestRecoverRunMode)
{
    RunMode runMode = MEM_POOL_MODE;

    RecoverRunMode(runMode);
    EXPECT_EQ(runMode, GetRunMode());
    EXPECT_EQ(runMode == WATERLINE_MODE, GetAdaptMem());
}

extern "C" size_t CalcNumaPayloadLen(void);
extern "C" size_t CalcNumaConfigLen(void);
extern "C" bool IsNumaConfigValid(char *numaBase, size_t totalLen);
TEST_F(SmapConfigTest, TestIsNumaConfigValid)
{
    bool ret;
    size_t totalLen;
    size_t numaPayloadLen = 32;
    struct PayloadHeader header = {0};

    header.len = (uint32_t)numaPayloadLen;
    totalLen = (size_t)numaPayloadLen + PAYLOAD_HEADER_LEN + CONFIG_HEADER_LEN;
    MOCKER(CalcNumaPayloadLen).stubs().will(returnValue(numaPayloadLen));
    MOCKER(CalcNumaConfigLen).stubs().will(returnValue(numaPayloadLen + PAYLOAD_HEADER_LEN));
    ret = IsNumaConfigValid((char *)&header, totalLen);
    EXPECT_TRUE(ret);

    header.len--;
    ret = IsNumaConfigValid((char *)&header, totalLen);
    EXPECT_FALSE(ret);
}

extern "C" int RecoverNumaConfig(char *numaBase);
TEST_F(SmapConfigTest, TestRecoverNumaConfig)
{
    int ret;
    int nrLocal = 2;
    int local1 = 0;
    int local2 = 1;
    int remote1 = 5;
    int remote2 = 7;
    char numaBase;
    struct ProcessManager manager; memset(&manager, 0, sizeof(manager));
    manager.remoteNumaInfo.privateSize =
        (uint64_t *)calloc(NM_COUNT(nrLocal), sizeof(uint64_t));
    manager.remoteNumaInfo.privateUsedInfo =
        (struct RemoteNumaUsedInfo *)calloc(NM_COUNT(nrLocal), sizeof(struct RemoteNumaUsedInfo));
    ASSERT_NE(nullptr, manager.remoteNumaInfo.privateSize);
    ASSERT_NE(nullptr, manager.remoteNumaInfo.privateUsedInfo);
    struct NumaPayload *payload =
        (struct NumaPayload *)calloc((nrLocal + 1) * SMAP_MAX_NUMNODES, sizeof(*payload));
    ASSERT_NE(nullptr, payload);
    payload[local1 * SMAP_MAX_NUMNODES + remote1].size = 1024; // local1-remote1 has 1024MB
    payload[local2 * SMAP_MAX_NUMNODES + remote2].size = 512; // local2-remote2 has 512MB

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(nrLocal));
    MOCKER(JumpToNumaPayload).stubs().will(returnValue(reinterpret_cast<char *>(payload)));
    ret = RecoverNumaConfig(&numaBase);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(1024U, NM_AT(manager.remoteNumaInfo.privateSize, local1, remote1));
    EXPECT_EQ(512U, NM_AT(manager.remoteNumaInfo.privateSize, local2, remote2));
    EXPECT_EQ(0U, NM_AT(manager.remoteNumaInfo.privateSize, local1, remote2));
    free(payload);
    free(manager.remoteNumaInfo.privateSize);
    free(manager.remoteNumaInfo.privateUsedInfo);
    manager.remoteNumaInfo.privateSize = nullptr;
    manager.remoteNumaInfo.privateUsedInfo = nullptr;
}

extern "C" void WriteOneNumaConfig(char *numaBase, struct NumaPayload *np);
TEST_F(SmapConfigTest, TestWriteOneNumaConfig)
{
    int nrLocal = 2;
    int index;
    uint8_t localNid = 1;
    uint8_t remoteNid = 6;
    uint32_t size = 128;
    char numaBase;
    struct NumaPayload np = {.local = localNid, .remote = remoteNid, .size = size};
    struct NumaPayload *payload;

    payload = (struct NumaPayload *)calloc((nrLocal + 1) * SMAP_MAX_NUMNODES, sizeof(*payload));
    ASSERT_NE(nullptr, payload);
    for (int i = 0; i < nrLocal; i++) {
        for (int j = 0; j < SMAP_MAX_NUMNODES; j++) {
            payload[i * SMAP_MAX_NUMNODES + j].local = i;
            payload[i * SMAP_MAX_NUMNODES + j].remote = j;
            payload[i * SMAP_MAX_NUMNODES + j].size = 0;
        }
    }
    index = localNid * SMAP_MAX_NUMNODES + remoteNid;
    ASSERT_EQ(0, payload[index].size);

    MOCKER(GetNrLocalNuma).stubs().will(returnValue(nrLocal));
    MOCKER(JumpToNumaPayload).stubs().will(returnValue(reinterpret_cast<char *>(payload)));
    WriteOneNumaConfig(&numaBase, &np);
    EXPECT_EQ(size, payload[index].size);

    free(payload);
}

extern "C" int AssignProcessAttr(ProcessAttr *attr, const struct ProcessPayloadHeader *header,
                                 const NumaBitmap *numaNodes, const struct PersistRemoteTarget *targets);
TEST_F(SmapConfigTest, TestAssignProcessAttr)
{
    ProcessAttr attr;
    struct ProcessPayloadHeader header = {};
    header.pid = 1025;
    header.scanType = NORMAL_SCAN;
    header.type = VM_TYPE;
    header.state = PROC_MOVE;
    header.migrateMode = MIG_RATIO_MODE;
    header.srcNid = NUMA_NO_NODE;
    header.scanTime = 200;
    header.duration = 0;
    header.nrWords = NumaWordsForCap();
    header.count = 1;
    struct PersistRemoteTarget targets[1] = {{ .nid = 4, .ratio = 50, .memSize = 0 }};
    NumaBitmap bm = {};
    ASSERT_TRUE(NumaBitmapAlloc(&bm));
    NumaBitmapSet(&bm, 0);
    NumaBitmapSet(&bm, 4);

    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    memset(&attr, 0, sizeof(ProcessAttr));
    ASSERT_EQ(attr.pid, 0);
    ASSERT_EQ(attr.initLocalMemRatio, 0);
    ASSERT_EQ(attr.type, 0);
    ASSERT_EQ(attr.state, 0);
    ASSERT_EQ(attr.scanType, 0);
    ASSERT_EQ(attr.scanTime, 0);
    ASSERT_EQ(attr.isFirstScan, false);
    ASSERT_EQ(attr.enableSwap, false);
    ASSERT_EQ(0, InitProcessAttrArrays(&attr));
    EXPECT_EQ(0, AssignProcessAttr(&attr, &header, &bm, targets));
    EXPECT_EQ(attr.pid, header.pid);
    EXPECT_EQ(attr.initLocalMemRatio, targets[0].ratio);
    EXPECT_EQ(attr.type, header.type);
    EXPECT_EQ(attr.state, header.state);
    EXPECT_EQ(attr.scanType, header.scanType);
    EXPECT_EQ(attr.scanTime, header.scanTime);
    EXPECT_TRUE(NumaBitmapTest(&attr.numaAttr.numaNodes, 0));
    EXPECT_TRUE(NumaBitmapTest(&attr.numaAttr.numaNodes, 4));
    EXPECT_EQ(attr.numaAttr.numaNodes.nWords, NumaWordsForCap());
    EXPECT_EQ(NUMA_NO_NODE, attr.targetConfig.sourceLocalNid);
    EXPECT_EQ(attr.isFirstScan, true);
    EXPECT_EQ(attr.enableSwap, true);
    FreeProcessAttrArrays(&attr);
    NumaBitmapFree(&bm);
}

TEST_F(SmapConfigTest, TestAssignProcessAttrRestoresIgnoreRemoteCapacity)
{
    ProcessAttr attr;
    struct ProcessPayloadHeader header = {};
    header.pid = 1025;
    header.scanType = NORMAL_SCAN;
    header.type = PROCESS_TYPE;
    header.state = PROC_IDLE;
    header.migrateMode = MIG_RATIO_MODE;
    header.nrWords = NumaWordsForCap();
    header.count = 0;
    header.ignoreRemoteCapacity = 1;
    struct PersistRemoteTarget targets[1] = {};
    NumaBitmap bm = {};
    ASSERT_TRUE(NumaBitmapAlloc(&bm));

    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    memset(&attr, 0, sizeof(ProcessAttr));
    EXPECT_EQ(0, AssignProcessAttr(&attr, &header, &bm, targets));
    EXPECT_TRUE(attr.ignoreRemoteCapacity);
    NumaBitmapFree(&attr.numaAttr.numaNodes);
    NumaBitmapFree(&bm);
}

TEST_F(SmapConfigTest, TestAssignProcessAttrIgnoreRemoteCapacityDefault)
{
    ProcessAttr attr;
    struct ProcessPayloadHeader header = {};
    header.pid = 1025;
    header.scanType = NORMAL_SCAN;
    header.type = PROCESS_TYPE;
    header.state = PROC_IDLE;
    header.migrateMode = MIG_RATIO_MODE;
    header.nrWords = NumaWordsForCap();
    header.count = 0;
    header.ignoreRemoteCapacity = 0;
    struct PersistRemoteTarget targets[1] = {};
    NumaBitmap bm = {};
    ASSERT_TRUE(NumaBitmapAlloc(&bm));

    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    memset(&attr, 0, sizeof(ProcessAttr));
    EXPECT_EQ(0, AssignProcessAttr(&attr, &header, &bm, targets));
    EXPECT_FALSE(attr.ignoreRemoteCapacity);
    NumaBitmapFree(&attr.numaAttr.numaNodes);
    NumaBitmapFree(&bm);
}

extern "C" int RestoreProcessTargetConfig(ProcessTargetConfig *config, uint8_t migrateMode, int srcNid, int count,
                                          const struct PersistRemoteTarget targets[]);
extern "C" int WriteTargetConfigToPayload(struct PersistRemoteTarget payloadTargets[], int *payloadCount,
                                          uint8_t *payloadMode, const ProcessTargetConfig *config);
TEST_F(SmapConfigTest, TestPersistedRemoteNidBeyondCapRejected)
{
    uint32_t oldCap = g_numaCap;
    g_numaCap = 64;
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));

    ProcessTargetConfig config;
    struct PersistRemoteTarget targets[1] = {{ .nid = 500, .ratio = 10, .memSize = 0 }};
    EXPECT_EQ(-EINVAL, RestoreProcessTargetConfig(&config, MIG_RATIO_MODE, NUMA_NO_NODE, 1, targets));
    targets[0].nid = 64;
    EXPECT_EQ(-EINVAL, RestoreProcessTargetConfig(&config, MIG_RATIO_MODE, NUMA_NO_NODE, 1, targets));
    targets[0].nid = 63;
    EXPECT_EQ(0, RestoreProcessTargetConfig(&config, MIG_RATIO_MODE, NUMA_NO_NODE, 1, targets));

    ProcessTargetConfig active;
    InitProcessTargetConfig(&active);
    active.migrateMode = MIG_RATIO_MODE;
    active.count = 1;
    active.targets[0].remoteNid = 500;
    active.targets[0].ratio = 10;
    struct PersistRemoteTarget out[SMAP_MAX_NUMNODES] = {};
    int count = 0;
    uint8_t mode = 0;
    EXPECT_EQ(-EINVAL, WriteTargetConfigToPayload(out, &count, &mode, &active));
    active.targets[0].remoteNid = 63;
    EXPECT_EQ(0, WriteTargetConfigToPayload(out, &count, &mode, &active));
    EXPECT_EQ(63, out[0].nid);

    g_numaCap = oldCap;
}

extern "C" size_t CalcNumaConfigLen(void);
extern "C" bool IsProcessConfigValid(char *processBase, size_t totalLen);
TEST_F(SmapConfigTest, TestIsProcessConfigValid)
{
    size_t numaConfigLen = 10;
    MOCKER(CalcNumaConfigLen).stubs().will(returnValue(numaConfigLen));

    uint32_t nrWords = NumaWordsForCap();
    size_t stride = CONFIG_PROC_HEADER_LEN + (size_t)nrWords * sizeof(unsigned long);
    char *buf = (char *)calloc(1, PAYLOAD_HEADER_LEN + stride);
    ASSERT_NE(nullptr, buf);
    struct PayloadHeader *header = (struct PayloadHeader *)buf;
    header->len = (uint32_t)stride;
    struct ProcessPayloadHeader *ph = (struct ProcessPayloadHeader *)(buf + PAYLOAD_HEADER_LEN);
    ph->nrWords = nrWords;
    ph->count = 0;

    size_t totalLen = (size_t)header->len + PAYLOAD_HEADER_LEN + CONFIG_HEADER_LEN + numaConfigLen;
    EXPECT_TRUE(IsProcessConfigValid(buf, totalLen));

    header->len--;
    EXPECT_FALSE(IsProcessConfigValid(buf, totalLen));
    free(buf);
}

extern "C" bool HasProcessConfig(size_t totalLen);
TEST_F(SmapConfigTest, TestHasProcessConfig)
{
    bool ret;
    size_t numaConfigLen = 10;
    size_t totalLen = numaConfigLen + CONFIG_HEADER_LEN;

    MOCKER(CalcNumaConfigLen).stubs().will(returnValue(numaConfigLen));
    ret = HasProcessConfig(totalLen);
    EXPECT_FALSE(ret);

    totalLen++;
    ret = HasProcessConfig(totalLen);
    EXPECT_TRUE(ret);
}

extern "C" char *JumpToProcessPayload(char *processBase);
extern "C" int RecoverProcessConfig(char *processBase);
TEST_F(SmapConfigTest, TestRecoverProcessConfig)
{
    int ret;
    int nrProcess = 2;
    ProcessAttr *attr;
    struct ProcessManager manager; memset(&manager, 0, sizeof(manager));

    struct PersistRemoteTarget t0[1] = {{ .nid = 4, .ratio = 25, .memSize = 0 }};
    struct PersistRemoteTarget t1[1] = {{ .nid = 5, .ratio = 15, .memSize = 0 }};
    const int bits0[] = {0, 4};
    const int bits1[] = {0, 5};
    alignas(8) char buf[1024] = {};
    size_t off = BuildV3Record(buf, 1025, NORMAL_SCAN, PROCESS_TYPE, PROC_IDLE, MIG_RATIO_MODE, 200, bits0, 2,
                               t0, 1);
    off += BuildV3Record(buf + off, 1026, NORMAL_SCAN, PROCESS_TYPE, PROC_IDLE, MIG_RATIO_MODE, 200, bits1, 2,
                         t1, 1);
    struct PayloadHeader header = {.len = (uint32_t)off};

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    MOCKER(JumpToProcessPayload).stubs().will(returnValue(static_cast<char *>(buf)));
    MOCKER(InitProcessMigrationTargetState).expects(exactly(nrProcess));
    MOCKER(IsHugeMode).stubs().will(returnValue(false));
    MOCKER(IsPidUsingHugePages).stubs().will(returnValue((int)false));
    ret = RecoverProcessConfig((char *)&header);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(nrProcess, manager.nr[PROCESS_TYPE]);
    EXPECT_EQ(1025, PmHeadAttr(&manager)->pid);
    EXPECT_EQ(1026, PmNthAttr(&manager, 1)->pid);

    attr = PmHeadAttr(&manager);
    while (attr) {
        PidSlotRemove(&manager, (attr)->pid);
        attr = PmHeadAttr(&manager);
    }
    ASSERT_EQ(nullptr, PmHeadAttr(&manager));
}

TEST_F(SmapConfigTest, TestRecoverProcessConfigTwo)
{
    int ret;
    struct ProcessManager manager; memset(&manager, 0, sizeof(manager));
    struct PersistRemoteTarget t0[1] = {{ .nid = 4, .ratio = 25, .memSize = 0 }};
    const int bits0[] = {0, 4};
    alignas(8) char buf[512] = {};
    size_t off = BuildV3Record(buf, 1025, NORMAL_SCAN, PROCESS_TYPE, PROC_IDLE, MIG_RATIO_MODE, 200, bits0, 2,
                               t0, 1);
    struct PayloadHeader header = {.len = (uint32_t)off};

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    MOCKER(JumpToProcessPayload).stubs().will(returnValue(static_cast<char *>(buf)));
    MOCKER(calloc).stubs().will(returnValue(static_cast<void *>(nullptr)));
    MOCKER(IsHugeMode).stubs().will(returnValue(false));
    MOCKER(IsPidUsingHugePages).stubs().will(returnValue((int)false));
    ret = RecoverProcessConfig((char *)&header);
    EXPECT_EQ(-ENOMEM, ret);
    ASSERT_EQ(nullptr, PmHeadAttr(&manager));
}

TEST_F(SmapConfigTest, TestRecoverProcessConfigPidNotExist)
{
    /* 进程已退出（-ESRCH）时同样跳过恢复，不进入纳管流程 */
    int ret;
    struct ProcessManager manager; memset(&manager, 0, sizeof(manager));
    struct PersistRemoteTarget t0[1] = {{ .nid = 4, .ratio = 25, .memSize = 0 }};
    const int bits[] = {0, 4};
    alignas(8) char buf[512] = {};
    size_t off = BuildV3Record(buf, 1025, NORMAL_SCAN, PROCESS_TYPE, PROC_IDLE, MIG_RATIO_MODE, 200, bits, 2,
                               t0, 1);
    struct PayloadHeader header = {.len = (uint32_t)off};

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    MOCKER(JumpToProcessPayload).stubs().will(returnValue(static_cast<char *>(buf)));
    MOCKER(InitProcessMigrationTargetState).expects(never());
    MOCKER(IsHugeMode).stubs().will(returnValue(false));
    MOCKER(IsPidUsingHugePages).stubs().will(returnValue(-ESRCH));
    ret = RecoverProcessConfig((char *)&header);
    EXPECT_EQ(0, ret);
    ASSERT_EQ(nullptr, PmHeadAttr(&manager));
}

TEST_F(SmapConfigTest, TestRecoverProcessConfigRestoresPairMultiRemoteTarget)
{
    struct ProcessManager manager; memset(&manager, 0, sizeof(manager));
    /* 0x53 = bits 0,1,4,6 */
    struct PersistRemoteTarget t0[2] = {{ .nid = 4, .ratio = 0, .memSize = 2048 },
                                        { .nid = 6, .ratio = 0, .memSize = 4096 }};
    const int bits[] = {0, 1, 4, 6};
    alignas(8) char buf[512] = {};
    size_t off = BuildV3Record(buf, 1234, NORMAL_SCAN, PROCESS_TYPE, PROC_IDLE, MIG_MEMSIZE_MODE, 200, bits, 4,
                               t0, 2);
    /* 记录迁出源本地 NUMA，校验恢复后 sourceLocalNid 生效 */
    reinterpret_cast<struct ProcessPayloadHeader *>(buf)->srcNid = 2;
    struct PayloadHeader header = {.len = (uint32_t)off};

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    MOCKER(GetPageSize).stubs().will(returnValue(static_cast<uint32_t>(PAGESIZE_4K)));
    MOCKER(JumpToProcessPayload).stubs().will(returnValue(static_cast<char *>(buf)));
    MOCKER(IsHugeMode).stubs().will(returnValue(false));
    MOCKER(IsPidUsingHugePages).stubs().will(returnValue((int)false));

    EXPECT_EQ(0, RecoverProcessConfig((char *)&header));
    ASSERT_NE(nullptr, PmHeadAttr(&manager));
    EXPECT_EQ(2U, PmHeadAttr(&manager)->targetConfig.count);
    EXPECT_EQ(MIG_MEMSIZE_MODE, PmHeadAttr(&manager)->targetConfig.migrateMode);
    EXPECT_EQ(2, PmHeadAttr(&manager)->targetConfig.sourceLocalNid);
    EXPECT_TRUE(NumaBitmapTest(&PmHeadAttr(&manager)->numaAttr.numaNodes, 0));
    EXPECT_TRUE(NumaBitmapTest(&PmHeadAttr(&manager)->numaAttr.numaNodes, 1));
    EXPECT_TRUE(NumaBitmapTest(&PmHeadAttr(&manager)->numaAttr.numaNodes, 4));
    EXPECT_TRUE(NumaBitmapTest(&PmHeadAttr(&manager)->numaAttr.numaNodes, 6));
    EXPECT_EQ(4, PmHeadAttr(&manager)->targetConfig.targets[0].remoteNid);
    EXPECT_EQ(2048U, PmHeadAttr(&manager)->targetConfig.targets[0].memSizeKB);
    EXPECT_EQ(6, PmHeadAttr(&manager)->targetConfig.targets[1].remoteNid);
    EXPECT_EQ(4096U, PmHeadAttr(&manager)->targetConfig.targets[1].memSizeKB);
    EXPECT_EQ(4, PmHeadAttr(&manager)->migrateParam[0].nid);
    EXPECT_EQ(6, PmHeadAttr(&manager)->migrateParam[1].nid);

    ProcessAttr *attr = PmHeadAttr(&manager);
    PidSlotRemove(&manager, (attr)->pid);
}

TEST_F(SmapConfigTest, TestRecoverProcessConfigSkipsDuplicatedPid)
{
    struct ProcessManager manager; memset(&manager, 0, sizeof(manager));
    struct PersistRemoteTarget t0[1] = {{ .nid = 4, .ratio = 25, .memSize = 0 }};
    struct PersistRemoteTarget t1[1] = {{ .nid = 5, .ratio = 15, .memSize = 0 }};
    const int bits0[] = {0, 4};
    const int bits1[] = {0, 5};
    alignas(8) char buf[1024] = {};
    size_t off = BuildV3Record(buf, 1025, NORMAL_SCAN, PROCESS_TYPE, PROC_IDLE, MIG_RATIO_MODE, 200, bits0, 2,
                               t0, 1);
    off += BuildV3Record(buf + off, 1025, NORMAL_SCAN, PROCESS_TYPE, PROC_IDLE, MIG_RATIO_MODE, 200, bits1, 2,
                         t1, 1);
    struct PayloadHeader header = {.len = (uint32_t)off};

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    MOCKER(JumpToProcessPayload).stubs().will(returnValue(static_cast<char *>(buf)));
    MOCKER(IsHugeMode).stubs().will(returnValue(false));
    MOCKER(IsPidUsingHugePages).stubs().will(returnValue((int)false));

    EXPECT_EQ(0, RecoverProcessConfig((char *)&header));
    EXPECT_EQ(1, manager.nr[PROCESS_TYPE]);
    ASSERT_NE(nullptr, PmHeadAttr(&manager));
    EXPECT_EQ(nullptr, PmNthAttr(&manager, 1));
    /* 只恢复第一条记录 */
    EXPECT_EQ(4, PmHeadAttr(&manager)->targetConfig.targets[0].remoteNid);
    PidSlotRemove(&manager, PmHeadAttr(&manager)->pid);
}

TEST_F(SmapConfigTest, TestRecoverProcessConfigNoFreeSlot)
{
    struct ProcessManager manager; memset(&manager, 0, sizeof(manager));
    struct PersistRemoteTarget t0[1] = {{ .nid = 4, .ratio = 25, .memSize = 0 }};
    const int bits0[] = {0, 4};
    alignas(8) char buf[512] = {};
    size_t off = BuildV3Record(buf, 1025, NORMAL_SCAN, PROCESS_TYPE, PROC_IDLE, MIG_RATIO_MODE, 200, bits0, 2,
                               t0, 1);
    struct PayloadHeader header = {.len = (uint32_t)off};

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    MOCKER(JumpToProcessPayload).stubs().will(returnValue(static_cast<char *>(buf)));
    MOCKER(IsHugeMode).stubs().will(returnValue(false));
    MOCKER(IsPidUsingHugePages).stubs().will(returnValue((int)false));
    MOCKER(PidSlotAdd).stubs().will(returnValue(-1));

    EXPECT_EQ(0, RecoverProcessConfig((char *)&header));
    EXPECT_EQ(0, manager.nr[PROCESS_TYPE]);
    EXPECT_EQ(0, manager.nr[VM_TYPE]);
    EXPECT_EQ(nullptr, PmHeadAttr(&manager));
}

extern "C" int WriteProcessConfig(char *processBase, const struct ProcessPayload *p, int nrPayload);
TEST_F(SmapConfigTest, TestWriteProcessConfig)
{
    int ret;
    int nrProcess = 2;
    struct PayloadHeader header = {};
    struct ProcessPayload np[2] = {};
    ASSERT_TRUE(NumaBitmapAlloc(&np[0].numaNodes));
    ASSERT_TRUE(NumaBitmapAlloc(&np[1].numaNodes));
    np[0].pid = 1025;
    np[0].scanType = 25;
    np[0].type = NORMAL_SCAN;
    np[0].state = VM_TYPE;
    np[0].migrateMode = 1;
    NumaBitmapSet(&np[0].numaNodes, 0);
    NumaBitmapSet(&np[0].numaNodes, 2);
    np[0].scanTime = 200;
    np[1].pid = 1026;
    np[1].scanType = 15;
    np[1].type = NORMAL_SCAN;
    np[1].state = VM_TYPE;
    np[1].migrateMode = 2;
    NumaBitmapSet(&np[1].numaNodes, 1);
    NumaBitmapSet(&np[1].numaNodes, 2);
    np[1].scanTime = 200;

    uint32_t nrWords = NumaWordsForCap();
    size_t stride = CONFIG_PROC_HEADER_LEN + (size_t)nrWords * sizeof(unsigned long);
    char *payload = (char *)calloc(nrProcess, stride);
    ASSERT_NE(nullptr, payload);

    MOCKER(JumpToProcessPayload).stubs().will(returnValue(payload));
    ret = WriteProcessConfig((char *)&header, np, nrProcess);
    EXPECT_EQ(0, ret);
    EXPECT_EQ((uint32_t)(nrProcess * stride), header.len);

    struct ProcessPayloadHeader *h0 = (struct ProcessPayloadHeader *)payload;
    EXPECT_EQ(np[0].pid, h0->pid);
    EXPECT_EQ(nrWords, h0->nrWords);
    unsigned long *w0 = (unsigned long *)(payload + CONFIG_PROC_HEADER_LEN);
    EXPECT_TRUE((w0[0] & NID_MASK(0)) != 0);
    EXPECT_TRUE((w0[0] & NID_MASK(2)) != 0);
    struct ProcessPayloadHeader *h1 = (struct ProcessPayloadHeader *)(payload + stride);
    EXPECT_EQ(np[1].pid, h1->pid);
    EXPECT_EQ(nrWords, h1->nrWords);
    unsigned long *w1 = (unsigned long *)(payload + stride + CONFIG_PROC_HEADER_LEN);
    EXPECT_TRUE((w1[0] & NID_MASK(1)) != 0);
    EXPECT_TRUE((w1[0] & NID_MASK(2)) != 0);

    free(payload);
    NumaBitmapFree(&np[0].numaNodes);
    NumaBitmapFree(&np[1].numaNodes);
}

TEST_F(SmapConfigTest, TestWriteProcessConfigClearsStaleBitmap)
{
    struct PayloadHeader header = {};
    struct ProcessPayload np[1] = {};
    np[0].pid = 1027;

    uint32_t nrWords = NumaWordsForCap();
    size_t stride = CONFIG_PROC_HEADER_LEN + (size_t)nrWords * sizeof(unsigned long);
    char *payload = (char *)malloc(stride);
    ASSERT_NE(nullptr, payload);
    memset(payload, 0xff, stride);

    MOCKER(JumpToProcessPayload).stubs().will(returnValue(payload));
    EXPECT_EQ(0, WriteProcessConfig((char *)&header, np, 1));

    struct ProcessPayloadHeader *h = (struct ProcessPayloadHeader *)payload;
    EXPECT_EQ(np[0].pid, h->pid);
    for (size_t i = 0; i < sizeof(h->reserved); i++) {
        EXPECT_EQ(0, h->reserved[i]);
    }
    unsigned long *w = (unsigned long *)(payload + CONFIG_PROC_HEADER_LEN);
    for (uint32_t i = 0; i < nrWords; i++) {
        EXPECT_EQ(0UL, w[i]);
    }
    free(payload);
}

extern "C" int BuildAllProcessPayload(struct ProcessPayload **payload, int *len);
TEST_F(SmapConfigTest, TestBuildAllProcessPayload)
{
    int ret;
    int nrProcess = 2;
    int len;
    struct ProcessManager manager;
    memset(&manager, 0, sizeof(manager));
    manager.nr[0] = 0;
    manager.nr[1] = 2;
    ProcessAttr *attr;
    ProcessAttr *attr1 = nullptr;
    ProcessAttr *attr2 = nullptr;
    struct ProcessPayload *payload = nullptr;

    attr1 = (ProcessAttr *)malloc(sizeof(*attr1));
    attr2 = (ProcessAttr *)malloc(sizeof(*attr2));

    ASSERT_NE(nullptr, attr1);
    ASSERT_NE(nullptr, attr2);
    memset(attr1, 0, sizeof(*attr1));
    memset(attr2, 0, sizeof(*attr2));

    attr1->type = VM_TYPE;
    attr1->pid = 1025;
    attr1->initLocalMemRatio = 25;
    attr1->state = PROC_IDLE;
    attr1->scanTime = 200;
    attr1->scanType = NORMAL_SCAN;
    attr1->targetConfig.sourceLocalNid = NUMA_NO_NODE;
    ASSERT_TRUE(NumaBitmapAlloc(&attr1->numaAttr.numaNodes));
    /* 0x22 = bits 1,5 */
    NumaBitmapSet(&attr1->numaAttr.numaNodes, 1);
    NumaBitmapSet(&attr1->numaAttr.numaNodes, 5);
    attr2->type = VM_TYPE;
    attr2->pid = 1026;
    attr2->initLocalMemRatio = 15;
    attr2->state = PROC_IDLE;
    attr2->scanTime = 200;
    attr2->scanType = NORMAL_SCAN;
    attr2->targetConfig.sourceLocalNid = NUMA_NO_NODE;
    ASSERT_TRUE(NumaBitmapAlloc(&attr2->numaAttr.numaNodes));
    /* 0x44 = bits 2,6 */
    NumaBitmapSet(&attr2->numaAttr.numaNodes, 2);
    NumaBitmapSet(&attr2->numaAttr.numaNodes, 6);

    PidSlotAdd(&manager, attr1);
    PidSlotAdd(&manager, attr2);

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    ret = BuildAllProcessPayload(&payload, &len);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(2, len);
    EXPECT_NE(nullptr, payload);
    EXPECT_EQ(attr1->pid, payload[0].pid);
    EXPECT_EQ(attr2->pid, payload[1].pid);
    EXPECT_TRUE(NumaBitmapTest(&payload[0].numaNodes, 1));
    EXPECT_TRUE(NumaBitmapTest(&payload[0].numaNodes, 5));
    EXPECT_TRUE(NumaBitmapTest(&payload[1].numaNodes, 2));
    EXPECT_TRUE(NumaBitmapTest(&payload[1].numaNodes, 6));

    attr = PmHeadAttr(&manager);
    while (attr) {
        PidSlotRemove(&manager, (attr)->pid);
        attr = PmHeadAttr(&manager);
    }
    ASSERT_EQ(nullptr, PmHeadAttr(&manager));
    for (int i = 0; i < len; i++) {
        NumaBitmapFree(&payload[i].numaNodes);
    }
    free(payload);
}

TEST_F(SmapConfigTest, TestBuildAllProcessPayloadTwo)
{
    int ret;
    int len;
    struct ProcessManager manager;
    memset(&manager, 0, sizeof(manager));
    manager.nr[0] = 0;
    manager.nr[1] = 2;
    ProcessAttr *attr;
    ProcessAttr *attr1 = nullptr;
    ProcessAttr *attr2 = nullptr;
    struct ProcessPayload *payload = nullptr;

    attr1 = (ProcessAttr *)malloc(sizeof(*attr1));
    attr2 = (ProcessAttr *)malloc(sizeof(*attr2));

    ASSERT_NE(nullptr, attr1);
    ASSERT_NE(nullptr, attr2);
    memset(attr1, 0, sizeof(*attr1));
    memset(attr2, 0, sizeof(*attr2));

    attr1->type = PROCESS_TYPE;
    attr1->pid = 1234;
    attr1->initLocalMemRatio = 50;
    attr1->state = PROC_IDLE;
    attr1->scanTime = 2000;
    attr1->scanType = NORMAL_SCAN;
    attr1->targetConfig.sourceLocalNid = NUMA_NO_NODE;
    ASSERT_TRUE(NumaBitmapAlloc(&attr1->numaAttr.numaNodes));
    /* 0x22 = bits 1,5 */
    NumaBitmapSet(&attr1->numaAttr.numaNodes, 1);
    NumaBitmapSet(&attr1->numaAttr.numaNodes, 5);
    attr2->type = VM_TYPE;
    attr2->pid = 1235;
    attr2->initLocalMemRatio = 40;
    attr2->state = PROC_IDLE;
    attr2->scanTime = 3000;
    attr2->scanType = HAM_SCAN;
    attr2->targetConfig.sourceLocalNid = NUMA_NO_NODE;
    ASSERT_TRUE(NumaBitmapAlloc(&attr2->numaAttr.numaNodes));
    /* 0x44 = bits 2,6 */
    NumaBitmapSet(&attr2->numaAttr.numaNodes, 2);
    NumaBitmapSet(&attr2->numaAttr.numaNodes, 6);

    PidSlotAdd(&manager, attr1);
    PidSlotAdd(&manager, attr2);

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    ret = BuildAllProcessPayload(&payload, &len);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(2, len);
    EXPECT_NE(nullptr, payload);
    EXPECT_EQ(attr1->pid, payload[0].pid);
    EXPECT_EQ(attr2->pid, payload[1].pid);
    EXPECT_TRUE(NumaBitmapTest(&payload[0].numaNodes, 1));
    EXPECT_TRUE(NumaBitmapTest(&payload[0].numaNodes, 5));
    EXPECT_TRUE(NumaBitmapTest(&payload[1].numaNodes, 2));
    EXPECT_TRUE(NumaBitmapTest(&payload[1].numaNodes, 6));

    attr = PmHeadAttr(&manager);
    while (attr) {
        PidSlotRemove(&manager, (attr)->pid);
        attr = PmHeadAttr(&manager);
    }
    ASSERT_EQ(nullptr, PmHeadAttr(&manager));
    for (int i = 0; i < len; i++) {
        NumaBitmapFree(&payload[i].numaNodes);
    }
    free(payload);
}

TEST_F(SmapConfigTest, TestBuildAllProcessPayloadPersistsPendingPairTargetAsEffectiveTarget)
{
    struct ProcessManager manager;
    memset(&manager, 0, sizeof(manager));
    ProcessAttr attr = {};
    struct ProcessPayload *payload = nullptr;
    int len = 0;
    attr.type = PROCESS_TYPE;
    attr.pid = 1234;
    attr.scanType = NORMAL_SCAN;
    attr.state = PROC_MIGRATE;
    ASSERT_TRUE(NumaBitmapAlloc(&attr.numaAttr.numaNodes));
    /* 0x53 = bits 0,1,4,6 */
    NumaBitmapSet(&attr.numaAttr.numaNodes, 0);
    NumaBitmapSet(&attr.numaAttr.numaNodes, 1);
    NumaBitmapSet(&attr.numaAttr.numaNodes, 4);
    NumaBitmapSet(&attr.numaAttr.numaNodes, 6);
    attr.targetConfig.migrateMode = MIG_MEMSIZE_MODE;
    attr.targetConfig.sourceLocalNid = 0;
    attr.targetConfig.count = 2;
    attr.targetConfig.targets[0] = {4, 0, 2048};
    attr.targetConfig.targets[1] = {6, 0, 4096};
    attr.pendingTargetConfigValid = true;
    attr.pendingTargetConfig.migrateMode = MIG_MEMSIZE_MODE;
    attr.pendingTargetConfig.sourceLocalNid = 2;
    attr.pendingTargetConfig.count = 1;
    attr.pendingTargetConfig.targets[0] = {8, 0, 8192};
    memset(&manager.slots, 0, sizeof(manager.slots));
    PidSlotAdd(&manager, &attr);

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    MOCKER(GetPageSize).stubs().will(returnValue(static_cast<uint32_t>(PAGESIZE_4K)));

    EXPECT_EQ(0, BuildAllProcessPayload(&payload, &len));
    ASSERT_NE(nullptr, payload);
    EXPECT_EQ(1, len);
    EXPECT_EQ(PROC_IDLE, payload[0].state);
    /* numaAttr.numaNodes is user-synthesized; persisted value is the last read-back result. */
    EXPECT_EQ(0x53UL, payload[0].numaNodes.bits[0]);
    EXPECT_EQ(MIG_MEMSIZE_MODE, payload[0].migrateMode);
    EXPECT_EQ(2, payload[0].srcNid);
    EXPECT_EQ(1, payload[0].count);
    EXPECT_EQ(8, payload[0].migrateParam[0].nid);
    EXPECT_EQ(8192U, payload[0].migrateParam[0].memSize);
    NumaBitmapFree(&payload[0].numaNodes);
    free(payload);
    NumaBitmapFree(&attr.numaAttr.numaNodes);
}

TEST_F(SmapConfigTest, TestBuildAllProcessPayloadPersistsIgnoreRemoteCapacity)
{
    struct ProcessManager manager;
    memset(&manager, 0, sizeof(manager));
    ProcessAttr attr = {};
    struct ProcessPayload *payload = nullptr;
    int len = 0;
    attr.type = PROCESS_TYPE;
    attr.pid = 1234;
    attr.scanType = NORMAL_SCAN;
    attr.state = PROC_IDLE;
    attr.migrateMode = MIG_RATIO_MODE;
    attr.ignoreRemoteCapacity = true;
    memset(&manager.slots, 0, sizeof(manager.slots));
    PidSlotAdd(&manager, &attr);

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));

    EXPECT_EQ(0, BuildAllProcessPayload(&payload, &len));
    ASSERT_NE(nullptr, payload);
    EXPECT_EQ(1, len);
    EXPECT_EQ(1, payload[0].ignoreRemoteCapacity);
    free(payload);
}

TEST_F(SmapConfigTest, TestBuildAllProcessPayloadPersistsPendingIgnoreRemoteCapacity)
{
    struct ProcessManager manager;
    memset(&manager, 0, sizeof(manager));
    ProcessAttr attr = {};
    struct ProcessPayload *payload = nullptr;
    int len = 0;
    attr.type = PROCESS_TYPE;
    attr.pid = 1234;
    attr.scanType = NORMAL_SCAN;
    attr.state = PROC_MIGRATE;
    attr.migrateMode = MIG_RATIO_MODE;
    attr.ignoreRemoteCapacity = false;
    attr.pendingTargetConfigValid = true;
    attr.pendingIgnoreRemoteCapacity = true;
    memset(&manager.slots, 0, sizeof(manager.slots));
    PidSlotAdd(&manager, &attr);

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));

    EXPECT_EQ(0, BuildAllProcessPayload(&payload, &len));
    ASSERT_NE(nullptr, payload);
    EXPECT_EQ(1, len);
    EXPECT_EQ(PROC_IDLE, payload[0].state);
    EXPECT_EQ(1, payload[0].ignoreRemoteCapacity);
    free(payload);
}

TEST_F(SmapConfigTest, TestBuildAllProcessPayloadSkipsGroupedProcess)
{
    struct ProcessManager manager;
    memset(&manager, 0, sizeof(manager));
    ProcessAttr grouped = {};
    ProcessAttr normal = {};
    struct ProcessPayload *payload = nullptr;
    int len = 0;
    grouped.type = VM_TYPE;
    grouped.pid = 2001;
    grouped.groupPolicy.enabled = true;
    grouped.groupPolicy.groupCount = 1;
    normal.type = VM_TYPE;
    normal.pid = 2002;
    normal.targetConfig.migrateMode = MIG_RATIO_MODE;
    normal.targetConfig.sourceLocalNid = NUMA_NO_NODE;
    PidSlotAdd(&manager, &grouped);
    PidSlotAdd(&manager, &normal);

    MOCKER(GetProcessManager).stubs().will(returnValue(&manager));
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));

    EXPECT_EQ(0, BuildAllProcessPayload(&payload, &len));
    ASSERT_NE(nullptr, payload);
    EXPECT_EQ(1, len);
    EXPECT_EQ(2002, payload[0].pid);
    /* 快照结束后槽位锁已释放 */
    EXPECT_EQ(0, pthread_mutex_trylock(&manager.slots[0].attrLock.lock));
    pthread_mutex_unlock(&manager.slots[0].attrLock.lock);
    free(payload);

    /* 只剩分组进程：不产出载荷 */
    normal.groupPolicy.enabled = true;
    normal.groupPolicy.groupCount = 1;
    payload = nullptr;
    EXPECT_EQ(0, BuildAllProcessPayload(&payload, &len));
    EXPECT_EQ(nullptr, payload);
    EXPECT_EQ(0, len);
}

extern "C" char *JumpToNumaConfig(char *base);
extern "C" char *JumpToProcessConfig(char *base);
TEST_F(SmapConfigTest, TestSyncRunModeInvalidRunMode)
{
    int ret;
    int fd = 1;
    int runMode = -1;

    ret = SyncRunMode((RunMode)runMode);
    EXPECT_EQ(-EINVAL, ret);

    runMode = MAX_RUN_MODE;
    ret = SyncRunMode((RunMode)runMode);
    EXPECT_EQ(-EINVAL, ret);
}

TEST_F(SmapConfigTest, TestSyncOneNumaConfigInvalidLocal)
{
    int ret;
    int nrLocal = 2;
    int local = nrLocal;
    int remote = 6;
    size_t size = 256;

    MOCKER(GetNrLocalNuma).stubs().will(returnValue(nrLocal));
    ret = SyncOneNumaConfig(local, remote, size);
    EXPECT_EQ(-EINVAL, ret);
}

TEST_F(SmapConfigTest, TestSyncOneNumaConfigInvalidRemote)
{
    int ret;
    int nrLocal = 2;
    int local = 1;
    int remote = 1;
    size_t size = 256;

    MOCKER(GetNrLocalNuma).stubs().will(returnValue(nrLocal));
    ret = SyncOneNumaConfig(local, remote, size);
    EXPECT_EQ(-EINVAL, ret);

    remote = SMAP_MAX_NUMNODES;
    ret = SyncOneNumaConfig(local, remote, size);
    EXPECT_EQ(-EINVAL, ret);
}

extern "C" int SyncAllProcessConfigLocked(struct ProcessPayload *payload, int nrPayload);
extern "C" uint64_t g_smapConfigSyncSeq;
extern "C" uint64_t g_smapConfigWrittenSeq;
TEST_F(SmapConfigTest, TestSyncAllProcessConfigBuildFail)
{
    MOCKER(BuildAllProcessPayload).stubs().will(returnValue(-EINVAL));
    MOCKER(SyncAllProcessConfigLocked).expects(never());
    EXPECT_EQ(-EINVAL, SyncAllProcessConfig());
}

TEST_F(SmapConfigTest, TestSyncAllProcessConfigSkipsStaleSnapshot)
{
    uint64_t oldWritten = g_smapConfigWrittenSeq;
    /* 模拟一个更新的快照已经落盘：本次领到的序号更小，不得覆盖 */
    g_smapConfigWrittenSeq = g_smapConfigSyncSeq + 10;
    MOCKER(BuildAllProcessPayload).stubs().will(returnValue(0));
    MOCKER(SyncAllProcessConfigLocked).expects(never());
    EXPECT_EQ(0, SyncAllProcessConfig());
    GlobalMockObject::verify();

    /* 序号追上后正常落盘，并推进已落盘序号 */
    g_smapConfigWrittenSeq = g_smapConfigSyncSeq;
    MOCKER(BuildAllProcessPayload).stubs().will(returnValue(0));
    MOCKER(SyncAllProcessConfigLocked).expects(once()).will(returnValue(0));
    EXPECT_EQ(0, SyncAllProcessConfig());
    EXPECT_EQ(g_smapConfigSyncSeq, g_smapConfigWrittenSeq);
    g_smapConfigWrittenSeq = oldWritten > g_smapConfigWrittenSeq ? oldWritten : g_smapConfigWrittenSeq;
}

/* ---------------- 配置文件映像读写（真实文件 I/O，落在临时目录） ---------------- */
extern "C" const char *SmapConfigPath(void);
extern "C" const char *SmapConfigLegacyPath(void);
extern "C" int PrepareConfigDir(const char *dir);
extern "C" const char *g_smapConfigPath;
extern "C" int LoadConfigImage(const char *path, struct SmapConfigImage *img);
extern "C" int StoreConfigImage(const char *path, const struct SmapConfigImage *img);
extern "C" int BuildInitConfigImage(struct SmapConfigImage *img);
extern "C" void FreeConfigImage(struct SmapConfigImage *img);
extern "C" int ConfigWriteFull(int fd, const char *buf, size_t len);
extern "C" int ChangeProcessConfig(struct SmapConfigImage *img, struct ProcessPayload *payload, int nrPayload);
extern "C" int ParseConfig(const struct SmapConfigImage *img);
extern "C" int ChangeOneNumaConfig(struct SmapConfigImage *img, struct NumaPayload *payload);
extern "C" int SyncRunModeLocked(RunMode runMode);
extern "C" int SyncOneNumaConfigLocked(int local, int remote, size_t size);
extern "C" int RecoverFromConfigLocked(void);
extern "C" void WriteNumaConfig(char *base);
extern "C" void RecoverRunMode(RunMode runMode);
extern "C" int RecoverNumaConfig(char *numaBase);
extern "C" int RecoverProcessConfig(char *processBase);

/* WriteNumaConfig 依赖 remoteNumaInfo 二维数组；这里只写出合法的 NUMA 配置长度，载荷保持为 0 */
static void FakeWriteNumaConfig(char *base)
{
    reinterpret_cast<struct PayloadHeader *>(base)->len = (uint32_t)CalcNumaPayloadLen();
}

class SmapConfigFileTest : public ::testing::Test {
protected:
    char dir[64] = {};
    std::string path;
    std::string legacyPath;

    void SetUp() override
    {
        ASSERT_EQ(EOK, strcpy_s(dir, sizeof(dir), "/tmp/smap_cfg_ut_XXXXXX"));
        ASSERT_NE(nullptr, mkdtemp(dir));
        path = std::string(dir) + "/smap_config";
        legacyPath = std::string(dir) + "/legacy_smap_config";
        MOCKER(SmapConfigPath).stubs().will(returnValue(static_cast<const char *>(path.c_str())));
        /* 旧位置同样落在临时目录，避免读取/删除宿主机 /dev/shm 下的真实文件 */
        MOCKER(SmapConfigLegacyPath).stubs().will(returnValue(static_cast<const char *>(legacyPath.c_str())));
        MOCKER(GetNrLocalNuma).stubs().will(returnValue(1));
        MOCKER(WriteNumaConfig).stubs().will(invoke(FakeWriteNumaConfig));
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        DIR *d = opendir(dir);
        if (d != nullptr) {
            struct dirent *e;
            while ((e = readdir(d)) != nullptr) {
                if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
                    (void)unlink((std::string(dir) + "/" + e->d_name).c_str());
                }
            }
            closedir(d);
        }
        (void)rmdir(dir);
    }

    int CountFiles()
    {
        int n = 0;
        DIR *d = opendir(dir);
        if (d == nullptr) {
            return -1;
        }
        struct dirent *e;
        while ((e = readdir(d)) != nullptr) {
            if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
                n++;
            }
        }
        closedir(d);
        return n;
    }

    void WriteRaw(const void *data, size_t len)
    {
        int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
        ASSERT_GE(fd, 0);
        if (len > 0) {
            ASSERT_EQ((ssize_t)len, write(fd, data, len));
        }
        close(fd);
    }

    std::vector<char> ReadRaw()
    {
        std::vector<char> out;
        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            return out;
        }
        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            out.insert(out.end(), buf, buf + n);
        }
        close(fd);
        return out;
    }

    void StoreInit(RunMode mode)
    {
        struct SmapConfigImage img = {};
        ASSERT_EQ(0, BuildInitConfigImage(&img));
        reinterpret_cast<struct SmapConfigHeader *>(img.data)->runMode = mode;
        ASSERT_EQ(0, StoreConfigImage(path.c_str(), &img));
        FreeConfigImage(&img);
    }
};

TEST_F(SmapConfigFileTest, LoadMissingFile)
{
    struct SmapConfigImage img = {};
    EXPECT_EQ(-ENOENT, LoadConfigImage(path.c_str(), &img));
    EXPECT_EQ(nullptr, img.data);
}

TEST_F(SmapConfigFileTest, LoadEmptyOrShortFileIsInvalidNotFatal)
{
    /* 0 字节/短文件：旧实现 mmap 后读头部会 SIGBUS，现在应返回 -EINVAL */
    struct SmapConfigImage img = {};
    WriteRaw("", 0);
    EXPECT_EQ(-EINVAL, LoadConfigImage(path.c_str(), &img));
    WriteRaw("abcd", 4);
    EXPECT_EQ(-EINVAL, LoadConfigImage(path.c_str(), &img));
    EXPECT_EQ(nullptr, img.data);
}

TEST_F(SmapConfigFileTest, LoadRejectsTotalLenBeyondFileSize)
{
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, BuildInitConfigImage(&img));
    reinterpret_cast<struct SmapConfigHeader *>(img.data)->totalLen = (uint32_t)img.len + 64;
    WriteRaw(img.data, img.len);
    FreeConfigImage(&img);

    EXPECT_EQ(-EINVAL, LoadConfigImage(path.c_str(), &img));
}

TEST_F(SmapConfigFileTest, LoadAcceptsTrailingBytesFromLegacyShrinkFailure)
{
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, BuildInitConfigImage(&img));
    std::vector<char> raw(img.data, img.data + img.len);
    raw.resize(raw.size() + 32, 0x5a);
    size_t len = img.len;
    FreeConfigImage(&img);
    WriteRaw(raw.data(), raw.size());

    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    EXPECT_EQ(len, img.len);
    FreeConfigImage(&img);
}

TEST_F(SmapConfigFileTest, StoreAndLoadRoundTrip)
{
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, BuildInitConfigImage(&img));
    EXPECT_EQ(CalcConfigLen(0), img.len);
    ASSERT_EQ(0, StoreConfigImage(path.c_str(), &img));

    /* 临时文件已被 rename，目录中只有配置文件本身，权限 0600 */
    EXPECT_EQ(1, CountFiles());
    struct stat st;
    ASSERT_EQ(0, stat(path.c_str(), &st));
    EXPECT_EQ(0600U, st.st_mode & 0777U);
    EXPECT_EQ((off_t)img.len, st.st_size);

    struct SmapConfigImage loaded = {};
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &loaded));
    ASSERT_EQ(img.len, loaded.len);
    EXPECT_EQ(0, memcmp(img.data, loaded.data, img.len));
    FreeConfigImage(&img);
    FreeConfigImage(&loaded);
}

TEST_F(SmapConfigFileTest, StoreFailureKeepsOldFile)
{
    StoreInit(WATERLINE_MODE);
    std::vector<char> before = ReadRaw();

    struct SmapConfigImage img = {};
    ASSERT_EQ(0, BuildInitConfigImage(&img));
    reinterpret_cast<struct SmapConfigHeader *>(img.data)->runMode = MEM_POOL_MODE;
    /* tmpfs 写满：返回 -ENOSPC，原文件保持不变，临时文件被清理 */
    MOCKER(ConfigWriteFull).stubs().will(returnValue(-ENOSPC));
    EXPECT_EQ(-ENOSPC, StoreConfigImage(path.c_str(), &img));
    FreeConfigImage(&img);

    EXPECT_EQ(before, ReadRaw());
    EXPECT_EQ(1, CountFiles());
}

TEST_F(SmapConfigFileTest, SyncRunModeCreatesAndUpdatesFile)
{
    ASSERT_EQ(0, SyncRunModeLocked(MEM_POOL_MODE));
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    EXPECT_EQ(MEM_POOL_MODE, reinterpret_cast<struct SmapConfigHeader *>(img.data)->runMode);
    FreeConfigImage(&img);

    ASSERT_EQ(0, SyncRunModeLocked(WATERLINE_MODE));
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    EXPECT_EQ(WATERLINE_MODE, reinterpret_cast<struct SmapConfigHeader *>(img.data)->runMode);
    FreeConfigImage(&img);
}

TEST_F(SmapConfigFileTest, SyncRunModeRebuildsCorruptedFile)
{
    WriteRaw("", 0);
    ASSERT_EQ(0, SyncRunModeLocked(MEM_POOL_MODE));
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    EXPECT_EQ(CalcConfigLen(0), img.len);
    EXPECT_EQ(MEM_POOL_MODE, reinterpret_cast<struct SmapConfigHeader *>(img.data)->runMode);
    FreeConfigImage(&img);
}

TEST_F(SmapConfigFileTest, SyncOneNumaConfigWritesPayload)
{
    ASSERT_EQ(0, SyncOneNumaConfigLocked(0, 3, 256));
    ASSERT_EQ(0, SyncOneNumaConfigLocked(-1, 2, 128));
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    struct NumaPayload *np = reinterpret_cast<struct NumaPayload *>(JumpToNumaPayload(JumpToNumaConfig(img.data)));
    EXPECT_EQ(256U, np[3].size);
    /* 共享区紧跟 nrLocalNuma(1) 行私有区 */
    EXPECT_EQ(128U, np[SMAP_MAX_NUMNODES + 2].size);
    FreeConfigImage(&img);
}

TEST_F(SmapConfigFileTest, SyncAllProcessConfigGrowsAndShrinksFile)
{
    struct ProcessPayload p[1] = {};
    p[0].pid = 4321;
    p[0].type = VM_TYPE;
    p[0].srcNid = NUMA_NO_NODE;
    ASSERT_EQ(0, SyncAllProcessConfigLocked(p, 1));

    struct SmapConfigImage img = {};
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    size_t stride = CONFIG_PROC_HEADER_LEN + (size_t)NumaWordsForCap() * sizeof(unsigned long);
    EXPECT_EQ(CalcConfigLen(stride), img.len);
    char *processBase = JumpToProcessConfig(img.data);
    EXPECT_EQ((uint32_t)stride, reinterpret_cast<struct PayloadHeader *>(processBase)->len);
    EXPECT_EQ(4321, reinterpret_cast<struct ProcessPayloadHeader *>(JumpToProcessPayload(processBase))->pid);
    FreeConfigImage(&img);

    ASSERT_EQ(0, SyncAllProcessConfigLocked(nullptr, 0));
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    EXPECT_EQ(CalcConfigLen(0), img.len);
    FreeConfigImage(&img);
    struct stat st;
    ASSERT_EQ(0, stat(path.c_str(), &st));
    EXPECT_EQ((off_t)CalcConfigLen(0), st.st_size);
    EXPECT_EQ(1, CountFiles());
}

TEST_F(SmapConfigFileTest, ChangeOnInvalidImage)
{
    char shortBuf[4] = {};
    struct SmapConfigImage img = { shortBuf, sizeof(shortBuf) };
    EXPECT_EQ(-EINVAL, ChangeProcessConfig(&img, nullptr, 0));

    ASSERT_EQ(0, BuildInitConfigImage(&img));
    reinterpret_cast<struct PayloadHeader *>(JumpToNumaConfig(img.data))->len = 1;
    struct NumaPayload np = { 0, 3, 1 };
    EXPECT_EQ(-EINVAL, ChangeOneNumaConfig(&img, &np));
    FreeConfigImage(&img);
}

TEST_F(SmapConfigFileTest, ParseConfigPaths)
{
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, BuildInitConfigImage(&img));

    reinterpret_cast<struct SmapConfigHeader *>(img.data)->runMode = MAX_RUN_MODE;
    EXPECT_EQ(-EINVAL, ParseConfig(&img));

    reinterpret_cast<struct SmapConfigHeader *>(img.data)->runMode = WATERLINE_MODE;
    MOCKER(RecoverRunMode).stubs().will(ignoreReturnValue());
    MOCKER(RecoverNumaConfig).stubs().will(returnValue(-ENOENT));
    EXPECT_EQ(-ENOENT, ParseConfig(&img));
    GlobalMockObject::verify();

    MOCKER(GetNrLocalNuma).stubs().will(returnValue(1));
    MOCKER(RecoverRunMode).stubs().will(ignoreReturnValue());
    MOCKER(RecoverNumaConfig).stubs().will(returnValue(0));
    MOCKER(RecoverProcessConfig).expects(once()).will(returnValue(0));
    EXPECT_EQ(0, ParseConfig(&img));
    GlobalMockObject::verify();

    MOCKER(GetNrLocalNuma).stubs().will(returnValue(1));
    MOCKER(RecoverRunMode).stubs().will(ignoreReturnValue());
    MOCKER(RecoverNumaConfig).stubs().will(returnValue(0));
    MOCKER(RecoverProcessConfig).stubs().will(returnValue(-EPERM));
    EXPECT_EQ(-EPERM, ParseConfig(&img));
    FreeConfigImage(&img);
}

TEST_F(SmapConfigFileTest, RecoverCreatesMissingFile)
{
    MOCKER(ParseConfig).expects(never());
    ASSERT_EQ(0, RecoverFromConfigLocked());
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    EXPECT_EQ(CalcConfigLen(0), img.len);
    FreeConfigImage(&img);
}

TEST_F(SmapConfigFileTest, RecoverRebuildsEmptyFile)
{
    WriteRaw("", 0);
    MOCKER(ParseConfig).expects(never());
    ASSERT_EQ(0, RecoverFromConfigLocked());
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    FreeConfigImage(&img);
    /* 损坏文件改名备份而不是删除 */
    struct stat st;
    ASSERT_EQ(0, stat((path + SMAP_CONFIG_BAK_SUFFIX).c_str(), &st));
    EXPECT_EQ(0, st.st_size);
}

TEST_F(SmapConfigFileTest, LoadRejectsUnsupportedVersion)
{
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, BuildInitConfigImage(&img));
    reinterpret_cast<struct SmapConfigHeader *>(img.data)->ver = SMAP_CONFIG_VER_V1;
    WriteRaw(img.data, img.len);
    FreeConfigImage(&img);
    EXPECT_EQ(-EPROTONOSUPPORT, LoadConfigImage(path.c_str(), &img));
    EXPECT_EQ(nullptr, img.data);
}

TEST_F(SmapConfigFileTest, RecoverBacksUpUnsupportedVersion)
{
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, BuildInitConfigImage(&img));
    reinterpret_cast<struct SmapConfigHeader *>(img.data)->ver = SMAP_CONFIG_VER_V2;
    std::vector<char> old(img.data, img.data + img.len);
    WriteRaw(img.data, img.len);
    FreeConfigImage(&img);

    MOCKER(ParseConfig).expects(never());
    ASSERT_EQ(0, RecoverFromConfigLocked());
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    EXPECT_EQ(SMAP_CONFIG_VER, reinterpret_cast<struct SmapConfigHeader *>(img.data)->ver);
    FreeConfigImage(&img);

    std::string saved = path;
    path += SMAP_CONFIG_BAK_SUFFIX;
    EXPECT_EQ(old, ReadRaw());
    path = saved;
    EXPECT_EQ(2, CountFiles());
}

TEST_F(SmapConfigFileTest, SyncBacksUpCorruptedFile)
{
    WriteRaw("abcd", 4);
    ASSERT_EQ(0, SyncRunModeLocked(MEM_POOL_MODE));
    struct stat st;
    ASSERT_EQ(0, stat((path + SMAP_CONFIG_BAK_SUFFIX).c_str(), &st));
    EXPECT_EQ(4, st.st_size);
    EXPECT_EQ(2, CountFiles());
}

TEST_F(SmapConfigFileTest, RecoverParsesValidFileWithoutRewriting)
{
    StoreInit(MEM_POOL_MODE);
    std::vector<char> before = ReadRaw();
    MOCKER(ParseConfig).expects(once()).will(returnValue(0));
    ASSERT_EQ(0, RecoverFromConfigLocked());
    EXPECT_EQ(before, ReadRaw());
}

TEST_F(SmapConfigFileTest, RecoverRebuildsWhenParseFails)
{
    StoreInit(MEM_POOL_MODE);
    MOCKER(ParseConfig).expects(once()).will(returnValue(-EINVAL));
    MOCKER(GetRunMode).stubs().will(returnValue(WATERLINE_MODE));
    ASSERT_EQ(0, RecoverFromConfigLocked());
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    EXPECT_EQ(WATERLINE_MODE, reinterpret_cast<struct SmapConfigHeader *>(img.data)->runMode);
    FreeConfigImage(&img);
    /* 解析失败的旧文件保留为备份 */
    ASSERT_EQ(0, LoadConfigImage((path + SMAP_CONFIG_BAK_SUFFIX).c_str(), &img));
    EXPECT_EQ(MEM_POOL_MODE, reinterpret_cast<struct SmapConfigHeader *>(img.data)->runMode);
    FreeConfigImage(&img);
}

TEST_F(SmapConfigFileTest, LoadRejectsSymlinkAndLooseMode)
{
    StoreInit(WATERLINE_MODE);
    struct SmapConfigImage img = {};

    /* 组/其他用户可访问：可能被他人改写，拒绝 */
    ASSERT_EQ(0, chmod(path.c_str(), 0644));
    EXPECT_EQ(-EPERM, LoadConfigImage(path.c_str(), &img));
    ASSERT_EQ(0, chmod(path.c_str(), 0600));
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    FreeConfigImage(&img);

    /* 符号链接：不跟随 */
    std::string link = std::string(dir) + "/link_config";
    ASSERT_EQ(0, symlink(path.c_str(), link.c_str()));
    EXPECT_EQ(-EPERM, LoadConfigImage(link.c_str(), &img));
    EXPECT_EQ(nullptr, img.data);
}

TEST_F(SmapConfigFileTest, PrepareConfigDirChecksOwnerAndMode)
{
    std::string sub = std::string(dir) + "/run_smap";
    ASSERT_EQ(0, PrepareConfigDir(sub.c_str()));
    struct stat st;
    ASSERT_EQ(0, stat(sub.c_str(), &st));
    EXPECT_TRUE(S_ISDIR(st.st_mode));
    EXPECT_EQ(0U, st.st_mode & 077U);
    /* 已存在且合规：幂等 */
    EXPECT_EQ(0, PrepareConfigDir(sub.c_str()));

    ASSERT_EQ(0, chmod(sub.c_str(), 0777));
    EXPECT_EQ(-EPERM, PrepareConfigDir(sub.c_str()));
    ASSERT_EQ(0, rmdir(sub.c_str()));

    std::string link = std::string(dir) + "/run_smap_link";
    ASSERT_EQ(0, symlink(dir, link.c_str()));
    EXPECT_NE(0, PrepareConfigDir(link.c_str()));
}

TEST_F(SmapConfigFileTest, RecoverMigratesLegacyConfig)
{
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, BuildInitConfigImage(&img));
    reinterpret_cast<struct SmapConfigHeader *>(img.data)->runMode = MEM_POOL_MODE;
    ASSERT_EQ(0, StoreConfigImage(legacyPath.c_str(), &img));
    std::vector<char> legacy(img.data, img.data + img.len);
    FreeConfigImage(&img);

    MOCKER(ParseConfig).expects(once()).will(returnValue(0));
    ASSERT_EQ(0, RecoverFromConfigLocked());
    EXPECT_EQ(legacy, ReadRaw());
    struct stat st;
    EXPECT_NE(0, stat(legacyPath.c_str(), &st));
    EXPECT_EQ(1, CountFiles());
}

TEST_F(SmapConfigFileTest, RecoverIgnoresUntrustedLegacyConfig)
{
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, BuildInitConfigImage(&img));
    ASSERT_EQ(0, StoreConfigImage(legacyPath.c_str(), &img));
    FreeConfigImage(&img);
    ASSERT_EQ(0, chmod(legacyPath.c_str(), 0666));

    MOCKER(ParseConfig).expects(never());
    ASSERT_EQ(0, RecoverFromConfigLocked());
    ASSERT_EQ(0, LoadConfigImage(path.c_str(), &img));
    FreeConfigImage(&img);
    struct stat st;
    EXPECT_EQ(0, stat(legacyPath.c_str(), &st));
}

TEST_F(SmapConfigFileTest, RecoverKeepsLegacyConfigWhenParseFails)
{
    struct SmapConfigImage img = {};
    ASSERT_EQ(0, BuildInitConfigImage(&img));
    ASSERT_EQ(0, StoreConfigImage(legacyPath.c_str(), &img));
    FreeConfigImage(&img);

    MOCKER(ParseConfig).expects(once()).will(returnValue(-EINVAL));
    ASSERT_EQ(0, RecoverFromConfigLocked());
    struct stat st;
    EXPECT_EQ(0, stat(legacyPath.c_str(), &st));
    EXPECT_EQ(0, stat(path.c_str(), &st));
    EXPECT_NE(0, stat((path + SMAP_CONFIG_BAK_SUFFIX).c_str(), &st));
}

TEST_F(SmapConfigTest, TestSmapConfigPathPrefersPrivateDir)
{
    const char *saved = g_smapConfigPath;
    static const char legacy[] = "/tmp/smap_cfg_ut_legacy_path";

    g_smapConfigPath = nullptr;
    MOCKER(PrepareConfigDir).expects(once()).will(returnValue(0));
    EXPECT_STREQ(SMAP_CONFIG_PATH, SmapConfigPath());
    EXPECT_STREQ(SMAP_CONFIG_PATH, SmapConfigPath());
    GlobalMockObject::verify();

    /* 私有目录不可用：退回旧位置，且只判定一次 */
    g_smapConfigPath = nullptr;
    MOCKER(PrepareConfigDir).expects(once()).will(returnValue(-EACCES));
    MOCKER(SmapConfigLegacyPath).stubs().will(returnValue(static_cast<const char *>(legacy)));
    EXPECT_STREQ(legacy, SmapConfigPath());
    EXPECT_STREQ(legacy, SmapConfigPath());
    GlobalMockObject::verify();

    g_smapConfigPath = saved;
}
