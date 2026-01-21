/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_DEPENDS_COMPILER_H
#define __LINUX_DEPENDS_COMPILER_H

#include <linux/compiler_types.h>

# ifndef likely
#  define likely(x) (x)
# endif
# ifndef unlikely
#  define unlikely(x) (x)
# endif

#define __must_be_array(array)	BUILD_BUG_ON_ZERO(__same_type((array), &(array)[0]))


#endif
