/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VERSION_DEPENDS_H
#define _LINUX_VERSION_DEPENDS_H

/* Pin to 6.6.0 so all LINUX_VERSION_CODE == KERNEL_VERSION(6, 6, 0) guards
 * in the test stubs select the correct 6.6 code paths. */
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + ((c) > 255 ? 255 : (c)))
#define LINUX_VERSION_CODE KERNEL_VERSION(6, 6, 0)

#endif
