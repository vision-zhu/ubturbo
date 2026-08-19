/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef __THREAD_H__
#define __THREAD_H__

#include "smap_env.h"

int InitDaemonThread(struct ProcessManager *manager, uint32_t period);

int DestroyDaemonThread(struct ProcessManager *manager);
int InitEventLoop(struct ProcessManager *manager);
int DestroyEventLoop(struct ProcessManager *manager);
void *DaemonThreadMain(void *args);
int EventLoopRegisterPid(pid_t pid);
void EventLoopUnregisterSlot(struct PidSlot *slot);
void EventLoopUnregisterSlotLocked(struct PidSlot *slot);
void EventLoopResetEnqueued(struct PidSlot *slot);

#endif /* __THREAD_H__ */
