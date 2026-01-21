/* SPDX-License-Identifier: GPL-2.0-only */
#include <string.h>
#include <linux/types.h>

char *kasprintf(gfp_t gfp, const char *fmt, ...)
{
    char name[] = "tx";
    char *ret = (char *)malloc(sizeof(&name));
    if (!strcpy(ret, name)) {
        return NULL;
    }
    return ret;
}