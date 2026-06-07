#ifndef _UAPI_USER_TASKS_H
#define _UAPI_USER_TASKS_H

#include <stdint.h> // For uint8_t, uint32_t

// C 用户任务的声明
void user_task0(void *param);
void user_task1(void *param);
void user_task(void *param);
void test_syscalls_task(void *param);

#endif // _UAPI_USER_TASKS_H
