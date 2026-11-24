/*
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef __SMAP_ENV_H__
#define __SMAP_ENV_H__

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>
#include <time.h>

#include "common.h"

#ifdef CONFIG_X86
#define VM_NAME_STR "qemu-system-x86"
#else
#define VM_NAME_STR "qemu-system-aar"
#define VM_KVM_NAME_STR "qemu-kvm"
#endif

#define PID_NAME_LEN (15)
#define PID_KVM_NAME_LEN (8)
#define CMDLINE_LEN 4096
#define PATH_MAX 4096
#define HUNDRED 100
#define LITTLE_NUM (1.0e-10)
#define USEC_PER_MSEC 1000

/* Miscellaneous defines */
#define KIB (1ULL << 10)
#define MIB (1ULL << 20)
#define GIB (1ULL << 30)
#define TIB (1ULL << 40)

typedef enum {
    PAGETYPE_NORMAL = 0,
    PAGETYPE_HUGE = 1,
} PageType;

#define PAGESIZE_4K (4 * KIB)
#define PAGESIZE_64K (64 * KIB)
#define PAGESIZE_2M (2 * MIB)

#define MAX_NR_MIGOUT 40

#ifdef __cplusplus
typedef std::atomic<int> atomic_int;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MIG_RATIO_MODE = 0, // 按照比例迁移
    MIG_MEMSIZE_MODE, // 按照内存大小迁移
} MigrateMode;

/* MUTEX */
typedef struct {
    pthread_mutex_t lock;
} EnvMutex;

static inline int EnvMutexDestroy(EnvMutex *mutex)
{
    if (pthread_mutex_destroy(&mutex->lock)) {
        return 1;
    }

    return 0;
}

static inline int EnvMutexInit(EnvMutex *mutex)
{
    if (pthread_mutex_init(&mutex->lock, NULL)) {
        return 1;
    }

    return 0;
}

static inline void EnvMutexLock(EnvMutex *mutex)
{
    pthread_mutex_lock(&mutex->lock);
}

static inline void EnvMutexUnlock(EnvMutex *mutex)
{
    pthread_mutex_unlock(&mutex->lock);
}

/* COND */
typedef struct {
    pthread_cond_t cond;
} EnvCond;

static inline int EnvCondInit(EnvCond *cond)
{
    return pthread_cond_init(&cond->cond, NULL);
}

static inline int EnvCondDestroy(EnvCond *cond)
{
    return pthread_cond_destroy(&cond->cond);
}

static inline void EnvCondWait(EnvCond *cond, EnvMutex *mutex)
{
    pthread_cond_wait(&cond->cond, &mutex->lock);
}

static inline void EnvCondSignal(EnvCond *cond)
{
    pthread_cond_signal(&cond->cond);
}

static inline void EnvCondBroadcast(EnvCond *cond)
{
    pthread_cond_broadcast(&cond->cond);
}

/* 使用 CLOCK_MONOTONIC 计时的条件变量，供 EnvCondTimedWaitMs 使用，不受系统时间调整影响 */
static inline int EnvCondInitMonotonic(EnvCond *cond)
{
    pthread_condattr_t attr;
    int ret = pthread_condattr_init(&attr);
    if (ret) {
        return ret;
    }
    ret = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (ret == 0) {
        ret = pthread_cond_init(&cond->cond, &attr);
    }
    (void)pthread_condattr_destroy(&attr);
    return ret;
}

/* 须持 mutex 调用；cond 须由 EnvCondInitMonotonic 初始化。超时返回 ETIMEDOUT */
static inline int EnvCondTimedWaitMs(EnvCond *cond, EnvMutex *mutex, uint64_t ms)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += (time_t)(ms / 1000ULL);
    ts.tv_nsec += (long)(ms % 1000ULL) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(&cond->cond, &mutex->lock, &ts);
}

typedef struct {
    atomic_int counter;
} EnvAtomic;

static inline int EnvAtomicRead(const EnvAtomic *a)
{
    return atomic_load(&a->counter);
}

static inline void EnvAtomicSet(EnvAtomic *a, int i)
{
    atomic_store(&a->counter, i);
}

static inline int EnvAtomicCmpAndSwap(int oldValue, int newValue, EnvAtomic *a)
{
#ifdef __cplusplus
    int actual = oldValue;
    if (a->counter.compare_exchange_strong(actual, newValue)) {
        return oldValue;
    }
    return actual;
#else
    return __sync_val_compare_and_swap(&a->counter, oldValue, newValue); // C 中使用内置函数
#endif
}

static inline int EnvAtomicAdd(EnvAtomic *a, int i)
{
#ifdef __cplusplus
    return atomic_fetch_add(&a->counter, i);
#else
    return __sync_fetch_and_add(&a->counter, i);
#endif
}

static inline int EnvAtomicSub(EnvAtomic *a, int i)
{
#ifdef __cplusplus
    return atomic_fetch_sub(&a->counter, i);
#else
    return __sync_fetch_and_sub(&a->counter, i);
#endif
}
/* TIME */
static inline void EnvMsleep(uint64_t n)
{
    usleep(n * USEC_PER_MSEC);
}

#ifdef __cplusplus
}
#endif
#endif /* __SMAP_ENV_H__ */
