/*
 * Description: smap5.0 user thread ut code
 */

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

#include "manage/manage.h"
#include "manage/thread.h"
#include "manage/thread_pool.h"

using namespace std;

class ThreadTest : public ::testing::Test {
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

extern "C" void EnvMutexLock(EnvMutex *mutex);
extern "C" void EnvMutexUnlock(EnvMutex *mutex);
extern "C" void *ThreadMainEvent(void *args);

TEST_F(ThreadTest, TestThreadMainStoped)
{
    struct ProcessManager pm;
    memset(&pm, 0, sizeof(pm));
    EnvAtomicSet(&pm.scanMigrateStop, 1);
    pm.migPeriod = 50;

    void *ret = ThreadMainEvent(&pm);
    EXPECT_EQ(nullptr, ret);
}

TEST_F(ThreadTest, TestInitScanMigrateThread)
{
    struct ProcessManager pm;
    memset(&pm, 0, sizeof(pm));
    uint32_t period = 100;
    int ret;

    MOCKER(pthread_create).stubs().will(returnValue(0));
    ret = InitScanMigrateThread(&pm, period);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(period, pm.migPeriod);
    EXPECT_EQ(0, EnvAtomicRead(&pm.scanMigrateStop));
}

TEST_F(ThreadTest, TestInitScanMigrateThreadCreateFailed)
{
    struct ProcessManager pm;
    memset(&pm, 0, sizeof(pm));
    uint32_t period = 50;
    int ret;

    MOCKER(pthread_create).stubs().will(returnValue(EAGAIN));
    ret = InitScanMigrateThread(&pm, period);
    EXPECT_EQ(-EAGAIN, ret);
}

TEST_F(ThreadTest, TestDestroyScanMigrateThread)
{
    int ret;
    struct ProcessManager pm;
    memset(&pm, 0, sizeof(pm));

    // pthread_t 非零时才执行 join，模拟线程已成功创建
    pm.scanMigrateThread = (pthread_t)1;
    MOCKER(pthread_join).expects(once()).will(ignoreReturnValue());
    ret = DestroyScanMigrateThread(&pm);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(1, EnvAtomicRead(&pm.scanMigrateStop));
}

TEST_F(ThreadTest, TestThreadPoolDestroyIdempotent)
{
    struct ProcessManager *pm = (struct ProcessManager *)calloc(1, sizeof(struct ProcessManager));
    ASSERT_NE(nullptr, pm);

    ASSERT_EQ(0, ThreadPoolInit(pm, 1));
    EXPECT_TRUE(pm->threadPool.inited);
    ThreadPoolDestroy(pm);
    EXPECT_FALSE(pm->threadPool.inited);
    // 重复销毁不得再次 join / 销毁已销毁的锁
    MOCKER(pthread_join).expects(never());
    ThreadPoolDestroy(pm);
    free(pm);
}

TEST_F(ThreadTest, TestThreadPoolSubmitRejectedAfterStop)
{
    struct ProcessManager *pm = (struct ProcessManager *)calloc(1, sizeof(struct ProcessManager));
    ASSERT_NE(nullptr, pm);
    struct PidSlot slot;
    memset(&slot, 0, sizeof(slot));

    ASSERT_EQ(0, ThreadPoolInit(pm, 1));
    EnvAtomicSet(&pm->threadPool.stop, 1);
    EXPECT_EQ(-ESHUTDOWN, ThreadPoolSubmit(pm, &slot, 1));
    EXPECT_EQ(0U, pm->threadPool.count);
    ThreadPoolDestroy(pm);
    free(pm);
}

extern "C" struct ProcessManager g_processManager;
TEST_F(ThreadTest, TestInitEventLoopRegisterFailureNotFatal)
{
    struct ProcessManager *pm = &g_processManager;
    ProcessAttr attr;
    memset(&attr, 0, sizeof(attr));
    attr.pid = 0x7ffffff0; /* /proc/smap/<pid>/mem_freq 不存在，注册必然失败 */
    attr.scanType = NORMAL_SCAN;
    memset(pm->slots, 0, sizeof(pm->slots));
    for (int i = 0; i < MAX_PID_SLOTS; i++) {
        pm->slots[i].memFreqFd = -1;
    }
    pm->slots[0].pid = attr.pid;
    pm->slots[0].attr = &attr;
    EnvAtomicSet(&pm->slots[0].refs, 1);
    EnvAtomicSet(&pm->slots[0].state, PID_SLOT_INUSE);

    // 单个 pid 注册失败只告警，交由 EventLoopEnsureRegistered 重试，不得导致启动失败
    EXPECT_EQ(0, InitEventLoop(pm));
    EXPECT_TRUE(pm->eventLoop.inited);
    EXPECT_EQ(-1, pm->slots[0].memFreqFd);
    EXPECT_EQ(0, DestroyEventLoop(pm));
    EXPECT_FALSE(pm->eventLoop.inited);
    memset(pm->slots, 0, sizeof(pm->slots));
}

extern "C" int ManagerDaemonWork(struct ProcessManager *manager);
TEST_F(ThreadTest, TestDestroyScanMigrateThreadWakesLongPeriod)
{
    struct ProcessManager *pm = (struct ProcessManager *)calloc(1, sizeof(struct ProcessManager));
    ASSERT_NE(nullptr, pm);
    MOCKER(ManagerDaemonWork).stubs().will(returnValue(0));

    // 一小时的迁移周期：stop 必须唤醒周期等待而不是睡满整个周期
    ASSERT_EQ(0, InitScanMigrateThread(pm, 3600U * 1000U));
    struct timespec begin;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    EXPECT_EQ(0, DestroyScanMigrateThread(pm));
    clock_gettime(CLOCK_MONOTONIC, &end);
    EXPECT_LT(end.tv_sec - begin.tv_sec, 5);
    EXPECT_EQ(1, EnvAtomicRead(&pm->scanMigrateStop));
    free(pm);
}

TEST_F(ThreadTest, TestThreadPoolSubmitIndexWrapsWithinRing)
{
    struct ProcessManager *pm = (struct ProcessManager *)calloc(1, sizeof(struct ProcessManager));
    ASSERT_NE(nullptr, pm);
    struct PidSlot slot;
    memset(&slot, 0, sizeof(slot));
    ThreadPool *pool = &pm->threadPool;
    EnvMutexInit(&pool->lock);
    EnvCondInit(&pool->cond);

    pool->tail = MAX_PID_SLOTS - 1;
    pool->head = MAX_PID_SLOTS - 1;
    EXPECT_EQ(0, ThreadPoolSubmit(pm, &slot, 1));
    EXPECT_EQ(0, ThreadPoolSubmit(pm, &slot, 2));
    EXPECT_EQ(1U, pool->tail);
    EXPECT_EQ(1, pool->ring[MAX_PID_SLOTS - 1].pid);
    EXPECT_EQ(2, pool->ring[0].pid);
    EXPECT_EQ(2U, pool->count);
    EnvCondDestroy(&pool->cond);
    EnvMutexDestroy(&pool->lock);
    free(pm);
}

TEST_F(ThreadTest, TestEventLoopResetEnqueuedRearmsOneshot)
{
    struct ProcessManager *pm = &g_processManager;
    EventLoop savedLoop;
    memcpy(&savedLoop, &pm->eventLoop, sizeof(savedLoop));
    struct PidSlot slot;
    memset(&slot, 0, sizeof(slot));
    EnvMutexInit(&slot.attrLock);
    int ep = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(ep, 0);
    int efd = eventfd(1, EFD_CLOEXEC | EFD_NONBLOCK); /* 计数非零：持续可读，模拟 freq_ready 未清零 */
    ASSERT_GE(efd, 0);
    pm->eventLoop.epfd = ep;
    EnvAtomicSet(&pm->eventLoop.stop, 0);
    slot.memFreqFd = efd;
    EnvAtomicSet(&slot.state, PID_SLOT_INUSE);
    EnvAtomicSet(&slot.eventEnqueued, 1);
    struct epoll_event ev = {.events = EPOLLIN | EPOLLONESHOT, .data = {.ptr = &slot}};
    ASSERT_EQ(0, epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev));

    struct epoll_event out[1];
    EXPECT_EQ(1, epoll_wait(ep, out, 1, 0));
    // 派发后 oneshot 解除监听：排队期间不会反复返回同一 fd
    EXPECT_EQ(0, epoll_wait(ep, out, 1, 0));
    // worker 完成后复位去重标志并重新武装
    EventLoopResetEnqueued(&slot);
    EXPECT_EQ(0, EnvAtomicRead(&slot.eventEnqueued));
    EXPECT_EQ(1, epoll_wait(ep, out, 1, 0));

    close(efd);
    close(ep);
    EnvMutexDestroy(&slot.attrLock);
    memcpy(&pm->eventLoop, &savedLoop, sizeof(savedLoop));
}
