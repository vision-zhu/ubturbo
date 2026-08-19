/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <string.h>
#include "securec.h"
#include "manage.h"
#include "thread.h"
#include "thread_pool.h"
#include "strategy/migration.h"
#include "smap_log_core.h"
#include "smap_user_log.h"

void EventLoopResetEnqueued(struct PidSlot *slot)
{
    EnvAtomicSet(&slot->eventEnqueued, 0);
}

void EventLoopUnregisterSlotLocked(struct PidSlot *slot)
{
    EventLoop *l = &GetProcessManager()->eventLoop;
    if (slot->memFreqFd >= 0) {
        if (l->epfd >= 0)
            (void)epoll_ctl(l->epfd, EPOLL_CTL_DEL, slot->memFreqFd, NULL);
        close(slot->memFreqFd);
        slot->memFreqFd = -1;
    }
    EventLoopResetEnqueued(slot);
}

void EventLoopUnregisterSlot(struct PidSlot *slot)
{
    EnvMutexLock(&slot->attrLock);
    EventLoopUnregisterSlotLocked(slot);
    EnvMutexUnlock(&slot->attrLock);
}

int EventLoopRegisterPid(pid_t pid)
{
    EventLoop *l = &GetProcessManager()->eventLoop;
    struct PidSlot *slot;
    int ret = 0;

    if (l->epfd < 0 || EnvAtomicRead(&l->stop))
        return 0;
    slot = PidSlotGetRef(pid);
    if (!slot)
        return -ENOENT;

    char path[64];
    EnvMutexLock(&slot->attrLock);
    if (EnvAtomicRead(&l->stop) || EnvAtomicRead(&slot->state) != PID_SLOT_INUSE ||
        slot->attr->scanType != NORMAL_SCAN || slot->memFreqFd >= 0)
        goto out;
    snprintf(path, sizeof(path), "/proc/smap/%d/mem_freq", pid);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        ret = -errno;
        goto out;
    }
    /* epoll 事件配置：
     * - EPOLLIN  内核完成一轮访问频次统计后 /proc/smap/<pid>/mem_freq 变为可读，
     *            事件循环据此把扫描/迁移任务派发给线程池
     * - EPOLLERR/EPOLLHUP  被管理pid消亡或被移除时 proc 文件释放，fd 上报错误/挂断，
     *            事件循环对这些槽位做状态检查（非 INUSE 则跳过），避免死进程 fd 空转
     * - data.ptr 直接保存槽位指针，事件触发后 Loop 从 ev[i].data.ptr 取回 PidSlot，免去 fd->slot 反查 */
    struct epoll_event ev = { .events = EPOLLIN | EPOLLERR | EPOLLHUP, .data.ptr = slot };
    if (epoll_ctl(l->epfd, EPOLL_CTL_ADD, fd, &ev)) {
        close(fd);
        ret = -errno;
        goto out;
    }
    slot->memFreqFd = fd;
    EventLoopResetEnqueued(slot);
out:
    EnvMutexUnlock(&slot->attrLock);
    PidSlotReleaseRefs(&slot, 1);
    return ret;
}

static int EventLoopRegisterExistingPids(struct ProcessManager *manager)
{
    struct PidSlot *slots[MAX_PID_SLOTS];
    size_t count = PidSlotCollectRefs(manager, slots, MAX_PID_SLOTS);
    int ret = 0;

    for (size_t i = 0; i < count; i++) {
        int registerRet = EventLoopRegisterPid(slots[i]->pid);

        if (registerRet && ret == 0)
            ret = registerRet;
    }
    PidSlotReleaseRefs(slots, count);
    return ret;
}

/* 处理 wakeFd 唤醒事件：eventfd 计数 > 0 时 epoll 会一直视为可读（水平触发），
 * 必须 read 清空计数，否则事件循环将持续空转；EAGAIN 表示计数已为 0，属正常 */
static void EventLoopHandleWakeFd(EventLoop *l)
{
    uint64_t value;
    ssize_t readRet = read(l->wakeFd, &value, sizeof(value));
    if (readRet < 0 && errno != EAGAIN)
        SMAP_LOGGER_WARNING("Read event loop wake fd failed: %d.", errno);
}

/* 唤醒通知：向 eventfd 写计数使阻塞中的 epoll_wait 立即返回；
 * EAGAIN 表示计数器已满（唤醒本就生效），无需重试 */
static void EventLoopNotifyWakeFd(EventLoop *l)
{
    uint64_t value = 1;
    if (l->wakeFd >= 0 && write(l->wakeFd, &value, sizeof(value)) < 0 && errno != EAGAIN)
        SMAP_LOGGER_WARNING("Wake event loop failed: %d.", errno);
}

static void *Loop(void *arg)
{
    struct ProcessManager *m = arg;
    EventLoop *l = &m->eventLoop;
    struct epoll_event ev[MAX_POOL_WORKERS];
    while (!EnvAtomicRead(&l->stop)) {
        /* 睡眠等待内核完成 */
        int n = epoll_wait(l->epfd, ev, MAX_POOL_WORKERS, -1);
        if (n < 0) {
            /* epoll_wait 阻塞期间可被信号打断返回 -1/EINTR：
             * - EINTR 只是"等待被信号中断"，不是错误，应继续等待；
             *   若不重试，一次无关信号（如 SIGCHLD）就会让事件循环退出
             * - 其他 errno（如 EBADF）说明 epoll 本身已损坏，继续等没有意义 */
            if (errno != EINTR)
                break;
            continue;
        }
        for (int i = 0; i < n; i++) {
            if (EnvAtomicRead(&l->stop))
                break;
            struct PidSlot *slot = ev[i].data.ptr;
            /* data.ptr == NULL 约定为该事件来自 wakeFd（门铃），只用于主动唤醒，不产生任务 */
            if (slot == NULL) {
                EventLoopHandleWakeFd(l);
                continue;
            }
            if (!PidSlotTryGetRef(slot)) {
                continue;
            }
            EnvMutexLock(&slot->attrLock);
            if (EnvAtomicRead(&slot->state) == PID_SLOT_INUSE && EnvAtomicCmpAndSwap(0, 1, &slot->eventEnqueued) == 0) {
                pid_t pid = slot->pid;

                if (ThreadPoolSubmit(m, slot, pid) == 0) {
                    /* 入队成功：引用随任务转交给 worker（由 PidMigrationWork 归还），此处不释放 */
                    EnvMutexUnlock(&slot->attrLock);
                    continue;
                }
                /* 入队失败（队列满）：复位去重标志，随后与未入队路径共用下面的引用归还 */
                EventLoopResetEnqueued(slot);
            }
            PidSlotReleaseRefs(&slot, 1);
            EnvMutexUnlock(&slot->attrLock);
        }
    }
    return NULL;
}

void *DaemonThreadMain(void *args)
{
    struct ProcessManager *manager = args;

    SMAP_LOGGER_INFO("Manager daemon thread %lu created.", gettid());
    while (!EnvAtomicRead(&manager->stop)) {
        EnvMsleep(manager->daemonPeriod);
        ManagerDaemonWork(manager);
    }
    return NULL;
}

int InitDaemonThread(struct ProcessManager *manager, uint32_t period)
{
    int ret;
    manager->daemonPeriod = period;
    EnvAtomicSet(&manager->stop, 0);
    /* 先清零 pthread_t，确保 pthread_create 失败时不残留未定义值，
     * DestroyDaemonThread 通过判断 pthread_t 是否为零来跳过无效 join */
    (void)memset_s(&manager->daemonThread, sizeof(manager->daemonThread), 0,
                   sizeof(manager->daemonThread));
    ret = pthread_create(&manager->daemonThread, NULL, DaemonThreadMain, manager);
    if (ret) {
        SMAP_LOGGER_ERROR("Create scan migrate thread failed: %d.", ret);
        (void)memset_s(&manager->daemonThread, sizeof(manager->daemonThread), 0,
                       sizeof(manager->daemonThread));
        return -ret;
    }
    return 0;
}

int DestroyDaemonThread(struct ProcessManager *manager)
{
    pthread_t zeroThread;
    (void)memset_s(&zeroThread, sizeof(zeroThread), 0, sizeof(zeroThread));

    EnvAtomicSet(&manager->stop, 1);
    /* 仅在 pthread_t 非零（即线程已成功创建）时才 join，避免对未初始化的 pthread_t 执行未定义行为 */
    if (memcmp(&manager->daemonThread, &zeroThread, sizeof(pthread_t)) != 0) {
        pthread_join(manager->daemonThread, NULL);
    }
    SMAP_LOGGER_INFO("Manager daemon thread destroyed.");
    return 0;
}

/* 关闭 epoll/wake fd 并复位，供初始化失败回滚与正常销毁共用 */
static void EventLoopCloseFds(EventLoop *l)
{
    if (l->wakeFd >= 0) {
        close(l->wakeFd);
        l->wakeFd = -1;
    }
    if (l->epfd >= 0) {
        close(l->epfd);
        l->epfd = -1;
    }
}

/* 置停止标志并唤醒事件循环线程，等待其退出 */
static void EventLoopStopAndJoin(EventLoop *l)
{
    EnvAtomicSet(&l->stop, 1);
    EventLoopNotifyWakeFd(l);
    pthread_join(l->thread, NULL);
}

int InitEventLoop(struct ProcessManager *m)
{
    EventLoop *l = &m->eventLoop;
    memset(l, 0, sizeof(*l));
    l->epfd = -1;
    l->wakeFd = -1;
    l->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (l->epfd < 0)
        return -errno;
    l->wakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (l->wakeFd < 0) {
        int ret = -errno;
        EventLoopCloseFds(l);
        return ret;
    }
    struct epoll_event wakeEvent = { .events = EPOLLIN, .data.ptr = NULL };
    if (epoll_ctl(l->epfd, EPOLL_CTL_ADD, l->wakeFd, &wakeEvent)) {
        int ret = -errno;
        EventLoopCloseFds(l);
        return ret;
    }
    int ret = pthread_create(&l->thread, NULL, Loop, m);
    if (ret) {
        EventLoopCloseFds(l);
        return -ret;
    }
    ret = EventLoopRegisterExistingPids(m);
    if (ret) {
        EventLoopStopAndJoin(l);
        EventLoopCloseFds(l);
        return ret;
    }
    return 0;
}
int DestroyEventLoop(struct ProcessManager *m)
{
    EventLoop *l = &m->eventLoop;
    if (l->epfd < 0)
        return 0;
    EventLoopStopAndJoin(l);
    for (int i = 0; i < MAX_PID_SLOTS; i++)
        EventLoopUnregisterSlot(&m->slots[i]);
    EventLoopCloseFds(l);
    return 0;
}
