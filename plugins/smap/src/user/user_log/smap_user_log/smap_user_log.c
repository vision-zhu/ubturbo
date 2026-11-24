/*
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include "smap_log_core.h"
#include "securec.h"
#include "smap_user_log.h"

#define SMAP_MAX_FILE_SIZE (200 * 1024 * 1024) // 每个日志文件的最大大小
#define SMAP_MAX_FILE_NUM 50

#define SMAP_LOG_BUF_LEN 512

#define SMAP_LOG_MODULE_NAME "SMAP "

typedef enum {
    EXT_LOGGER_DEBUG = 0,
    EXT_LOGGER_INFO,
    EXT_LOGGER_WARNING,
    EXT_LOGGER_ERROR,
} ExtLoggerLevel;

typedef void (*Logfunc)(int level, const char *str, const char *moduleName);

/* 订阅回调可被 SmapLoggerExit 并发置空：统一原子读写，调用前先读到局部变量，避免两次读取之间被置空 */
static Logfunc g_subscribers;

static inline Logfunc LoadSubscriber(void)
{
    return __atomic_load_n(&g_subscribers, __ATOMIC_ACQUIRE);
}

void UpstreamSubscribeLogger(Logfunc extlog)
{
    __atomic_store_n(&g_subscribers, extlog, __ATOMIC_RELEASE);
}

int SmapStartULog(const char *ulogPath)
{
    char logBuf[SMAP_LOG_BUF_LEN];
    int ret;
    SmapLogConfig config;

    if (ulogPath == NULL) {
        return -EINVAL;
    }

    ret = sprintf_s(logBuf, SMAP_LOG_BUF_LEN, "%s", ulogPath);
    if (ret < 0) {
        return -1;
    }

#ifndef USE_DT
    errno_t res = strncpy_s(config.filePath, sizeof(config.filePath), logBuf, sizeof(config.filePath) - 1);
    if (res != EOK) {
        SMAP_LOGGER_ERROR("strncpy_s failed, ret(%d).", res);
        return -1;
    }
    config.maxFileSize = SMAP_MAX_FILE_SIZE;
    config.maxFileCount = SMAP_MAX_FILE_NUM;
    config.minLogLevel = SMAP_LOG_CORE_INFO; /* 默认INFO级别，DEBUG日志不输出 */

    ret = SmapLogCoreInit(&config);
    if (ret != 0) {
        return -EIO;
    }
#endif

    return 0;
}

static int LogLevelMapping(int logLevel)
{
    switch (logLevel) {
        case SMAP_LOG_CORE_TRACE:
        case SMAP_LOG_CORE_DEBUG:
            return EXT_LOGGER_DEBUG;
        case SMAP_LOG_CORE_INFO:
            return EXT_LOGGER_INFO;
        case SMAP_LOG_CORE_WARN:
            return EXT_LOGGER_WARNING;
        case SMAP_LOG_CORE_ERROR:
        default:
            return EXT_LOGGER_ERROR;
    }
}

static void SmapUlogInner(int logLevel, const char *prefix, const char *msg)
{
    if (logLevel < SmapLogCoreGetMinLogLevel()) {
        return;
    }
    Logfunc subscriber = LoadSubscriber();
    if (subscriber != NULL) {
        int level = LogLevelMapping(logLevel);
        char exLogMsg[SMAP_LOG_BUF_LEN] = SMAP_LOG_MODULE_NAME;
        errno_t ret = strncat_s(exLogMsg, sizeof(exLogMsg), prefix, SMAP_LOG_BUF_LEN - 1);
        if (ret) {
            return;
        }
        subscriber(level, msg, exLogMsg);
        return;
    }
#ifndef USE_DT
    SmapLogCoreWrite(logLevel, prefix, msg);
#endif
}

/* ToCoreLevel - SMAP_LOG_* 映射为日志核心级别，非法级别返回 -1 */
static int ToCoreLevel(int logLevel)
{
    switch (logLevel) {
        case SMAP_LOG_DEBUG:
            return SMAP_LOG_CORE_DEBUG;
        case SMAP_LOG_INFO:
            return SMAP_LOG_CORE_INFO;
        case SMAP_LOG_WARNING:
            return SMAP_LOG_CORE_WARN;
        case SMAP_LOG_ERROR:
            return SMAP_LOG_CORE_ERROR;
        default:
            return -1;
    }
}

void SMAP_Ulog(int logLevel, const char *funcName, int funcLine, const char *fileName, const char *format, ...)
{
    int ret;
    va_list argPtr;
    char head[SMAP_LOG_BUF_LEN] = { 0 };
    char data[SMAP_LOG_BUF_LEN] = { 0 };
    (void)funcName;

    /* 先按级别过滤再格式化：被过滤的 DEBUG 日志不再付出 vsnprintf 开销（热路径） */
    int coreLevel = ToCoreLevel(logLevel);
    if (coreLevel < 0 || coreLevel < SmapLogCoreGetMinLogLevel()) {
        return;
    }

    /* 超长内容截断保留（*_truncated_s 截断时返回 destMax - 1），而不是整条丢弃 */
    if (snprintf_truncated_s(head, sizeof(head), "%s %d", fileName, funcLine) < 0) {
        return;
    }
    va_start(argPtr, format);
    ret = vsnprintf_truncated_s(data, sizeof(data), format, argPtr);
    va_end(argPtr);
    if (ret < 0) {
#ifndef USE_DT
        SmapLogCoreWrite(SMAP_LOG_CORE_ERROR, head, "vsnprintf_truncated_s failed.");
#endif
        return;
    }

    SmapUlogInner(coreLevel, head, data);
}

void SmapLoggerExit(void)
{
    __atomic_store_n(&g_subscribers, NULL, __ATOMIC_RELEASE);
#ifndef USE_DT
    SmapLogCoreExit();
#endif
}