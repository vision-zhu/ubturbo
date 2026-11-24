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
#include "securec.h"
#include "thread_pool.h"
#include "thread.h"
#include "strategy/migration.h"
#include "smap_user_log.h"

/* 固定 worker 从环形队列取任务执行 per-pid 迁移流水；workers<=0 时取 CPU 核数 */
static void *Worker(void *arg)
{
    struct ProcessManager *manager = arg;
    ThreadPool *pool = &manager->threadPool;
    bool running = true;
    while (running) {
        EnvMutexLock(&pool->lock);
        while (!pool->count && !EnvAtomicRead(&pool->stop)) {
            EnvCondWait(&pool->cond, &pool->lock);
        }
        if (!pool->count && EnvAtomicRead(&pool->stop)) {
            running = false;
            EnvMutexUnlock(&pool->lock);
            continue;
        }
        ThreadPoolTask task = pool->ring[pool->head];
        pool->head = (pool->head + 1) % MAX_PID_SLOTS;
        pool->count--;
        EnvMutexUnlock(&pool->lock);
        PidMigrationWork(manager, task.slot, task.pid);
    }
    return NULL;
}

int ThreadPoolInit(struct ProcessManager *manager, int workers)
{
    if (manager == NULL) {
        return -EINVAL;
    }
    ThreadPool *pool = &manager->threadPool;
    if (workers <= 0) {
        long cpuCount = sysconf(_SC_NPROCESSORS_ONLN);
        workers = cpuCount > 0 ? (int)cpuCount : 1;
    }
    if (workers > MAX_POOL_WORKERS) {
        workers = MAX_POOL_WORKERS;
    }
    (void)memset_s(pool, sizeof(*pool), 0, sizeof(*pool));
    EnvMutexInit(&pool->lock);
    EnvCondInit(&pool->cond);
    pool->inited = true;
    pool->nrWorkers = workers;
    for (int i = 0; i < workers; i++) {
        int err = pthread_create(&pool->workers[i], NULL, Worker, manager);
        if (err) {
            /* 回滚已创建的 worker，复用完整销毁流程 */
            pool->nrWorkers = i;
            ThreadPoolDestroy(manager);
            return -err;
        }
    }
    return 0;
}

int ThreadPoolSubmit(struct ProcessManager *manager, struct PidSlot *slot, pid_t pid)
{
    if (manager == NULL || slot == NULL) {
        return -EINVAL;
    }

    ThreadPool *pool = &manager->threadPool;
    EnvMutexLock(&pool->lock);
    /* 已开始销毁：拒绝入队，避免任务（及其携带的槽位引用）落入无人消费的队列 */
    if (EnvAtomicRead(&pool->stop)) {
        EnvMutexUnlock(&pool->lock);
        return -ESHUTDOWN;
    }
    if (pool->count == MAX_PID_SLOTS) {
        EnvMutexUnlock(&pool->lock);
        return -EAGAIN;
    }
    /* head/tail 始终保持在 [0, MAX_PID_SLOTS) 内：容量非 2 的幂，自由递增的计数器回绕时下标会跳变 */
    pool->ring[pool->tail] = (ThreadPoolTask){ .slot = slot, .pid = pid };
    pool->tail = (pool->tail + 1) % MAX_PID_SLOTS;
    pool->count++;
    EnvCondSignal(&pool->cond);
    EnvMutexUnlock(&pool->lock);
    return 0;
}

void ThreadPoolDestroy(struct ProcessManager *manager)
{
    if (manager == NULL) {
        return;
    }

    ThreadPool *pool = &manager->threadPool;
    /* 幂等：Init 失败路径已自行销毁，调用方回滚时可能再次调用 */
    if (!pool->inited) {
        return;
    }
    EnvAtomicSet(&pool->stop, 1);
    EnvMutexLock(&pool->lock);
    EnvCondBroadcast(&pool->cond);
    EnvMutexUnlock(&pool->lock);
    for (int i = 0; i < pool->nrWorkers; i++) {
        pthread_join(pool->workers[i], NULL);
    }
    pool->nrWorkers = 0;
    EnvCondDestroy(&pool->cond);
    EnvMutexDestroy(&pool->lock);
    pool->inited = false;
}
