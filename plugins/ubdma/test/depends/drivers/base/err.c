/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/err.h>

bool IS_ERR(const void *ptr)
{
    return false;
}

long PTR_ERR(const void *ptr)
{
    return 0;
}