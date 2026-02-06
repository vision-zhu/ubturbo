/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TEST_UACCESS_H
#define TEST_UACCESS_H

#ifdef __cplusplus
extern "C" {
#endif
unsigned long copy_from_user(void *to, const void *from, unsigned long n);
#ifdef __cplusplus
}
#endif

#endif // TEST_UACCESS_H