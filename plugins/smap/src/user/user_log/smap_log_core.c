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

#include "smap_log_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

static SmapLogFile g_smapLogFile;
static int g_minLogLevel = SMAP_LOG_CORE_DEBUG;
static int g_initialized = 0;

static const char *LogLevelToString(int level)
{
    static const char *levelNames[] = {
        "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"
    };
    if (level >= 0 && level < SMAP_LOG_CORE_BUTT) {
        return levelNames[level];
    }
    return "UNKNOWN";
}

static void GetTimestamp(char *buffer, size_t bufSize)
{
    struct timespec ts;
    struct tm tm_info;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_info);

    snprintf(buffer, bufSize, "%04d-%02d-%02d %02d:%02d:%02d.%06ld",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
             ts.tv_nsec / 1000);
}

static FILE *OpenLogFile(const char *basePath, int index)
{
    char filePath[SMAP_LOG_MAX_PATH_LEN];
    FILE *fp = NULL;

    if (index == 0) {
        snprintf(filePath, sizeof(filePath), "%s", basePath);
    } else {
        snprintf(filePath, sizeof(filePath), "%s.%d", basePath, index);
    }

    fp = fopen(filePath, "a");
    if (fp == NULL) {
        return NULL;
    }

    return fp;
}

static size_t GetFileSize(const char *filePath)
{
    struct stat st;
    if (stat(filePath, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

static int RotateLogFile(void)
{
    char oldPath[SMAP_LOG_MAX_PATH_LEN];
    char newPath[SMAP_LOG_MAX_PATH_LEN];
    int i;

    if (g_smapLogFile.fileHandle != NULL) {
        fclose(g_smapLogFile.fileHandle);
        g_smapLogFile.fileHandle = NULL;
    }

    for (i = g_smapLogFile.maxFileCount - 1; i > 0; i--) {
        snprintf(oldPath, sizeof(oldPath), "%s.%d", g_smapLogFile.basePath, i - 1);
        snprintf(newPath, sizeof(newPath), "%s.%d", g_smapLogFile.basePath, i);

        if (access(oldPath, F_OK) == 0) {
            if (i == g_smapLogFile.maxFileCount - 1) {
                remove(newPath);
            }
            rename(oldPath, newPath);
        }
    }

    snprintf(newPath, sizeof(newPath), "%s.0", g_smapLogFile.basePath);
    if (access(g_smapLogFile.basePath, F_OK) == 0) {
        rename(g_smapLogFile.basePath, newPath);
    }

    g_smapLogFile.currentFileIndex = 0;
    g_smapLogFile.currentFileSize = 0;
    g_smapLogFile.fileHandle = OpenLogFile(g_smapLogFile.basePath, 0);

    return g_smapLogFile.fileHandle != NULL ? 0 : -1;
}

static int WriteLogHeader(FILE *fp)
{
    fprintf(fp, "Log started at [DEBUG] level\n");
    fprintf(fp, "Log default format: yyyy-mm-dd hh:mm:ss.uuuuuu threadid loglevel msg\n");
    fflush(fp);

    return 0;
}

int SmapLogCoreInit(const SmapLogConfig *config)
{
    if (config == NULL || config->filePath[0] == '\0') {
        return -EINVAL;
    }

    if (g_initialized) {
        return 0;
    }

    pthread_mutex_init(&g_smapLogFile.lock, NULL);

    strncpy(g_smapLogFile.basePath, config->filePath, sizeof(g_smapLogFile.basePath) - 1);
    g_smapLogFile.basePath[sizeof(g_smapLogFile.basePath) - 1] = '\0';

    g_smapLogFile.maxFileSize = config->maxFileSize > 0 ?
                                config->maxFileSize : SMAP_LOG_MAX_FILE_SIZE_DEFAULT;
    g_smapLogFile.maxFileCount = config->maxFileCount > 0 ?
                                 config->maxFileCount : SMAP_LOG_MAX_FILE_COUNT_DEFAULT;
    g_minLogLevel = config->minLogLevel;

    g_smapLogFile.currentFileIndex = 0;
    g_smapLogFile.currentFileSize = GetFileSize(config->filePath);
    g_smapLogFile.fileHandle = OpenLogFile(config->filePath, 0);

    if (g_smapLogFile.fileHandle == NULL) {
        pthread_mutex_destroy(&g_smapLogFile.lock);
        return -EIO;
    }

    WriteLogHeader(g_smapLogFile.fileHandle);
    g_initialized = 1;

    return 0;
}

void SmapLogCoreExit(void)
{
    if (!g_initialized) {
        return;
    }

    pthread_mutex_lock(&g_smapLogFile.lock);

    if (g_smapLogFile.fileHandle != NULL) {
        fflush(g_smapLogFile.fileHandle);
        fclose(g_smapLogFile.fileHandle);
        g_smapLogFile.fileHandle = NULL;
    }

    pthread_mutex_unlock(&g_smapLogFile.lock);
    pthread_mutex_destroy(&g_smapLogFile.lock);
    g_initialized = 0;
}

int SmapLogCoreWrite(int level, const char *prefix, const char *message)
{
    char timestamp[64];
    char logLine[SMAP_LOG_MAX_MSG_LEN * 2];
    size_t lineLen;

    if (!g_initialized) {
        return -EINVAL;
    }

    if (level < g_minLogLevel || level >= SMAP_LOG_CORE_BUTT) {
        return 0;
    }

    if (prefix == NULL || message == NULL) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_smapLogFile.lock);

    if (g_smapLogFile.fileHandle == NULL) {
        pthread_mutex_unlock(&g_smapLogFile.lock);
        return -EIO;
    }

    GetTimestamp(timestamp, sizeof(timestamp));

    snprintf(logLine, sizeof(logLine), "%s %lu %s [%s] %s\n",
             timestamp, (unsigned long)pthread_self(),
             LogLevelToString(level), prefix, message);

    lineLen = strlen(logLine);

    if (g_smapLogFile.currentFileSize + lineLen > g_smapLogFile.maxFileSize) {
        if (RotateLogFile() != 0) {
            pthread_mutex_unlock(&g_smapLogFile.lock);
            return -EIO;
        }
    }

    if (fwrite(logLine, 1, lineLen, g_smapLogFile.fileHandle) != lineLen) {
        pthread_mutex_unlock(&g_smapLogFile.lock);
        return -EIO;
    }

    g_smapLogFile.currentFileSize += lineLen;

    if (level >= SMAP_LOG_CORE_ERROR) {
        fflush(g_smapLogFile.fileHandle);
    }

    pthread_mutex_unlock(&g_smapLogFile.lock);

    return 0;
}

int SmapLogCoreGetMinLogLevel(void)
{
    return g_minLogLevel;
}