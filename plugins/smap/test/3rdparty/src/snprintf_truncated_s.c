/*
 * Description: snprintf_truncated_s / vsnprintf_truncated_s stub functions
 */

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include "securec.h"

/* 与 libboundscheck 语义一致：截断时保留已截断内容并返回 destMax - 1，出错返回 -1 */
int vsnprintf_truncated_s(char *strDest, size_t destMax, const char *format, va_list argList)
{
    if (strDest == NULL || destMax == 0 || format == NULL) {
        return -1;
    }
    int ret = vsnprintf(strDest, destMax, format, argList);
    if (ret < 0) {
        strDest[0] = '\0';
        return -1;
    }
    return (size_t)ret >= destMax ? (int)(destMax - 1) : ret;
}

int snprintf_truncated_s(char *strDest, size_t destMax, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsnprintf_truncated_s(strDest, destMax, format, args);
    va_end(args);
    return ret;
}
