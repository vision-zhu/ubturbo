/*
 * Description: smap5.0 user inner interface ut code
 */

#include <sys/time.h>
#include <cstdlib>
#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

#include "manage/manage.h"
#include "manage/node_matrix.h"
#include "smap_inner_interface.h"
#include "smap_log_core.h"

static inline NumaBitmap BmFromWord(unsigned long w)
{
    NumaBitmap bm = {};
    (void)NumaBitmapAlloc(&bm);
    if (bm.bits != NULL) {
        bm.bits[0] = w;
    }
    return bm;
}

const double TEST_DEFAULT_LOCAL_MEM_RATIO = 75.0;

using namespace std;
extern "C" EnvAtomic g_status;
extern "C" struct ProcessManager g_processManager;

class InnerInterfaceTest : public ::testing::Test {
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

TEST_F(InnerInterfaceTest, TestNullptrError)
{
    int ret;
    EnvAtomicSet(&g_status, 1);

    ret = SmapQueryVmMemRatio(nullptr);
    EXPECT_EQ(-EINVAL, ret);
}

extern "C" void SetAdaptMem(bool flag);
TEST_F(InnerInterfaceTest, TestSmapEnableAdaptMemOne)
{
    int ret;
    MOCKER(SetAdaptMem).stubs();
    ret = SmapEnableAdaptMem(0);
    EXPECT_EQ(0, ret);
}

TEST_F(InnerInterfaceTest, TestSmapEnableAdaptMemTwo)
{
    int ret;
    MOCKER(SetAdaptMem).stubs();
    ret = SmapEnableAdaptMem(1);
    EXPECT_EQ(0, ret);
}

TEST_F(InnerInterfaceTest, TestSmapEnableAdaptMemThree)
{
    int ret;
    MOCKER(SetAdaptMem).stubs();
    ret = SmapEnableAdaptMem(-1);
    EXPECT_EQ(-EINVAL, ret);
}

TEST_F(InnerInterfaceTest, TestSmapQueryVmMemRatio)
{
    EnvAtomicSet(&g_status, 0);
    int ret = SmapQueryVmMemRatio(nullptr);
    EXPECT_EQ(-EPERM, ret);

    EnvAtomicSet(&g_status, 1);
    struct VmRatioMsg msg;
    struct ProcessManager *manager = GetProcessManager();
    ProcessAttr current;
    current.next = nullptr;
    current.type = VM_TYPE;
    current.pid = 1;
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    ASSERT_EQ(0, InitProcessAttrArrays(&current));
    /* 0b00010001 = bits 0,4；InitProcessAttrArrays 会重新分配并清零，须在其后设置 */
    NumaBitmapSet(&current.numaAttr.numaNodes, 0);
    NumaBitmapSet(&current.numaAttr.numaNodes, 4);
    NM_AT(current.strategyAttr.l3RemoteMemRatio, 0, 4) = TEST_DEFAULT_LOCAL_MEM_RATIO;
    memset(&manager->slots, 0, sizeof(manager->slots)); PidSlotAdd(manager, &current);
    ret = SmapQueryVmMemRatio(&msg);
    EXPECT_EQ(1, msg.vr[0].pid);
    EXPECT_EQ(HUNDRED - TEST_DEFAULT_LOCAL_MEM_RATIO, msg.vr[0].ratio);
    FreeProcessAttrArrays(&current);
}

TEST_F(InnerInterfaceTest, TestSmapQueryVmMemRatioSkipsInvalidL1)
{
    EnvAtomicSet(&g_status, 1);
    struct VmRatioMsg msg;
    struct ProcessManager *manager = GetProcessManager();
    ProcessAttr proc = {};
    ProcessAttr vmNoL1 = {};
    ProcessAttr vm = {};
    proc.type = PROCESS_TYPE;
    proc.pid = 11;
    vmNoL1.type = VM_TYPE;
    vmNoL1.pid = 12;
    vm.type = VM_TYPE;
    vm.pid = 13;
    MOCKER(GetNrLocalNuma).stubs().will(returnValue(4));
    ASSERT_EQ(0, InitProcessAttrArrays(&proc));
    ASSERT_EQ(0, InitProcessAttrArrays(&vmNoL1));
    ASSERT_EQ(0, InitProcessAttrArrays(&vm));
    /* 普通进程与一台 VM 的 L1 尚未确定（首轮回读前位图为空） */
    NumaBitmapSet(&vm.numaAttr.numaNodes, 0);
    NumaBitmapSet(&vm.numaAttr.numaNodes, 4);
    NM_AT(vm.strategyAttr.l3RemoteMemRatio, 0, 4) = TEST_DEFAULT_LOCAL_MEM_RATIO;
    memset(&manager->slots, 0, sizeof(manager->slots));
    PidSlotAdd(manager, &proc);
    PidSlotAdd(manager, &vmNoL1);
    PidSlotAdd(manager, &vm);

    /* 与之无关的进程/VM 的 L1 为空不得让整个查询失败 */
    EXPECT_EQ(0, SmapQueryVmMemRatio(&msg));
    EXPECT_EQ(1, msg.nrVm);
    EXPECT_EQ(13, msg.vr[0].pid);
    EXPECT_EQ(HUNDRED - TEST_DEFAULT_LOCAL_MEM_RATIO, msg.vr[0].ratio);
    memset(&manager->slots, 0, sizeof(manager->slots));
    FreeProcessAttrArrays(&proc);
    FreeProcessAttrArrays(&vmNoL1);
    FreeProcessAttrArrays(&vm);
}

/* --- SmapSetLogLevel tests --- */

TEST_F(InnerInterfaceTest, TestSmapSetLogLevelValidDebug)
{
    SmapLogCoreSetMinLogLevel(SMAP_LOG_CORE_INFO);
    int ret = SmapSetLogLevel(SMAP_LOG_CORE_DEBUG);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(SMAP_LOG_CORE_DEBUG, SmapLogCoreGetMinLogLevel());
}

TEST_F(InnerInterfaceTest, TestSmapSetLogLevelValidInfo)
{
    SmapLogCoreSetMinLogLevel(SMAP_LOG_CORE_DEBUG);
    int ret = SmapSetLogLevel(SMAP_LOG_CORE_INFO);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(SMAP_LOG_CORE_INFO, SmapLogCoreGetMinLogLevel());
}

TEST_F(InnerInterfaceTest, TestSmapSetLogLevelValidWarn)
{
    int ret = SmapSetLogLevel(SMAP_LOG_CORE_WARN);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(SMAP_LOG_CORE_WARN, SmapLogCoreGetMinLogLevel());
}

TEST_F(InnerInterfaceTest, TestSmapSetLogLevelValidError)
{
    int ret = SmapSetLogLevel(SMAP_LOG_CORE_ERROR);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(SMAP_LOG_CORE_ERROR, SmapLogCoreGetMinLogLevel());
}

TEST_F(InnerInterfaceTest, TestSmapSetLogLevelInvalidNegative)
{
    int ret = SmapSetLogLevel(-1);
    EXPECT_EQ(-EINVAL, ret);
}

TEST_F(InnerInterfaceTest, TestSmapSetLogLevelInvalidTooLarge)
{
    int ret = SmapSetLogLevel(SMAP_LOG_CORE_BUTT);
    EXPECT_EQ(-EINVAL, ret);
}

TEST_F(InnerInterfaceTest, TestSmapSetLogLevelRevertDebugToInfo)
{
    /* Set to DEBUG first */
    SmapLogCoreSetMinLogLevel(SMAP_LOG_CORE_DEBUG);
    EXPECT_EQ(SMAP_LOG_CORE_DEBUG, SmapLogCoreGetMinLogLevel());

    /* Revert to INFO */
    int ret = SmapSetLogLevel(SMAP_LOG_CORE_INFO);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(SMAP_LOG_CORE_INFO, SmapLogCoreGetMinLogLevel());
}
