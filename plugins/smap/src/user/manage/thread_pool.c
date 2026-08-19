#include "thread_pool.h"
#include "strategy/migration.h"
#include <unistd.h>

static void *Worker(void *arg)
{
    struct ProcessManager *manager = arg;
    ThreadPool *pool = &manager->threadPool;
    for (;;) {
        EnvMutexLock(&pool->lock);
        while (!pool->count && !EnvAtomicRead(&pool->stop))
            EnvCondWait(&pool->cond, &pool->lock);
        if (!pool->count && EnvAtomicRead(&pool->stop)) {
            EnvMutexUnlock(&pool->lock);
            return NULL;
        }
        ThreadPoolTask task = pool->ring[pool->head++ % MAX_PID_SLOTS];
        pool->count--;
        EnvMutexUnlock(&pool->lock);
        PidMigrationWork(manager, task.slot, task.pid);
    }
}
int ThreadPoolInit(struct ProcessManager *manager, int workers)
{
    ThreadPool *pool = &manager->threadPool;
    if (workers <= 0) {
        long cpuCount = sysconf(_SC_NPROCESSORS_ONLN);
        workers = cpuCount > 0 ? (int)cpuCount : 1;
    }
    if (workers > MAX_POOL_WORKERS)
        workers = MAX_POOL_WORKERS;
    memset(pool, 0, sizeof(*pool));
    EnvMutexInit(&pool->lock);
    EnvCondInit(&pool->cond);
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
    ThreadPool *pool = &manager->threadPool;
    EnvMutexLock(&pool->lock);
    if (pool->count == MAX_PID_SLOTS) {
        EnvMutexUnlock(&pool->lock);
        return -EAGAIN;
    }
    pool->ring[pool->tail++ % MAX_PID_SLOTS] = (ThreadPoolTask){ .slot = slot, .pid = pid };
    pool->count++;
    EnvCondSignal(&pool->cond);
    EnvMutexUnlock(&pool->lock);
    return 0;
}
void ThreadPoolDestroy(struct ProcessManager *manager)
{
    ThreadPool *pool = &manager->threadPool;
    EnvAtomicSet(&pool->stop, 1);
    EnvMutexLock(&pool->lock);
    EnvCondBroadcast(&pool->cond);
    EnvMutexUnlock(&pool->lock);
    for (int i = 0; i < pool->nrWorkers; i++)
        pthread_join(pool->workers[i], NULL);
    pool->nrWorkers = 0;
    EnvCondDestroy(&pool->cond);
    EnvMutexDestroy(&pool->lock);
}
