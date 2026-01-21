/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPILER_TYPES_H
#define __LINUX_COMPILER_TYPES_H

#define BTF_TYPE_TAG(value)

#define __percpu	BTF_TYPE_TAG(percpu)
#define __user	BTF_TYPE_TAG(user) // BTF_TYPE_TAG(user)
#define __force

#endif
