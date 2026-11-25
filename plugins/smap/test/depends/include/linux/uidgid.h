/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UIDGID_H
#define _LINUX_UIDGID_H

#include <linux/types.h>

typedef struct {
	uid_t val;
} kuid_t;

typedef struct {
	gid_t val;
} kgid_t;

#define INVALID_UID ((uid_t)-1)
#define INVALID_GID ((gid_t)-1)

static inline bool uid_valid(kuid_t uid)
{
	return uid.val != INVALID_UID;
}

static inline bool gid_valid(kgid_t gid)
{
	return gid.val != INVALID_GID;
}

#endif /* _LINUX_UIDGID_H */
