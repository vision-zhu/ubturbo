/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_GFP_H
#define __LINUX_GFP_H

#include <linux/types.h>
#include <linux/gfp_types.h>

#define ___GFP_THISNODE 0x200000u

#define __GFP_THISNODE	((__force gfp_t)___GFP_THISNODE)

char *kasprintf(gfp_t gfp, const char *fmt, ...);

#endif /* __LINUX_GFP_H */
