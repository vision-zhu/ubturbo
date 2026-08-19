#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__
#include "manage.h"
int ThreadPoolInit(struct ProcessManager *manager, int workers);
int ThreadPoolSubmit(struct ProcessManager *manager, struct PidSlot *slot, pid_t pid);
void ThreadPoolDestroy(struct ProcessManager *manager);
#endif
