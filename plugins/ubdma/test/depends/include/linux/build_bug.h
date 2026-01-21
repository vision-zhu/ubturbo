/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BUILD_BUG_H
#define _LINUX_BUILD_BUG_H

#include <asm-generic/int-ll64.h>

#define BUILD_BUG_ON_ZERO(x) ((int)(sizeof(struct { int:(-!!(x)); })))

#endif