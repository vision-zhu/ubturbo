/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <linux/types.h>
#include <linux/printk.h>
#include <linux/err.h>
#include <linux/kdev_t.h>

struct file_system_type {
    const char *name;
    int fs_flags;
};

struct super_block {
    struct list_head    s_list; // Keep this first
    dev_t            s_dev; // search index; _not_ kdev_t
    unsigned char        s_blocksize_bits;
    unsigned long        s_blocksize;
    struct file_system_type *s_type;
};

struct inode {
    dev_t            i_rdev;
    void *i_private; // fs or device private pointer
    struct super_block *i_sb;
    struct cdev *i_cdev;
};

struct file {
    unsigned int         f_flags;
    /* needed for tty driver, and maybe others */
    void *private_data;
    struct inode *f_inode; // cached value
};

struct file_operations {
    long (*unlocked_ioctl) (struct file *f, unsigned int cmd, unsigned long arg);
    int (*release) (struct inode *i, struct file *f);
    int (*open) (struct inode *i, struct file *f);
    ssize_t (*write_iter) (struct kiocb *k, struct iov_iter *iter);
    /* file_operations module owner */
    struct module *owner;
    ssize_t (*read_iter) (struct kiocb *k, struct iov_iter *iter);
    ssize_t (*write) (struct file *f, const char __user *buf, size_t len, loff_t *off);
    ssize_t (*read) (struct file *f, char __user *buf, size_t len, loff_t *off);
    loff_t (*llseek) (struct file *f, loff_t off, int w);
};

#ifdef __cplusplus
extern "C" {
#endif
extern int alloc_chrdev_region(dev_t *, unsigned, unsigned, const char *);
extern void unregister_chrdev_region(dev_t, unsigned);
#ifdef __cplusplus
}
#endif

#endif
