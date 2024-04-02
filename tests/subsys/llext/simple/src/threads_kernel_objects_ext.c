/*
 * Copyright (c) 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This code demonstrates the use of threads and requires object
 * relocation support.
 */

#include <stdint.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/kernel.h>

#ifdef CONFIG_USERSPACE
extern struct k_sem my_sem_userspace;
extern struct k_thread my_thread_userspace;
#else
static struct k_thread my_thread;
static struct k_sem my_sem;
#endif

extern k_thread_stack_t my_thread_stack;
#define STACK_SIZE 1024

void test_thread(void *arg0, void *arg1, void *arg2)
{
	printk("Take semaphore from test thread\n");
#ifdef CONFIG_USERSPACE
	k_sem_take(&my_sem_userspace, K_FOREVER);
#else
	k_sem_take(&my_sem, K_FOREVER);
#endif
}

void test_entry(void)
{
#ifdef CONFIG_USERSPACE
	printk("Give semaphore from main thread\n");
	k_sem_give(&my_sem_userspace);
#else
	printk("Initialize and give semaphore from main thread\n");
	k_sem_init(&my_sem, 1, 1);
	k_sem_give(&my_sem);
#endif

	printk("Creating thread\n");
#ifdef CONFIG_USERSPACE
	k_tid_t tid = k_thread_create(&my_thread_userspace, &my_thread_stack,
				STACK_SIZE, &test_thread, NULL, NULL, NULL,
				1, K_USER, K_FOREVER);
	k_object_access_grant(&my_sem_userspace, &my_thread_userspace);
#else
	k_tid_t tid = k_thread_create(&my_thread, &my_thread_stack,
				STACK_SIZE, &test_thread, NULL, NULL, NULL,
				0, 0, K_FOREVER);
#endif
	printk("Starting thread\n");
	k_thread_start(tid);
#ifdef CONFIG_USERSPACE
	k_thread_join(&my_thread_userspace, K_FOREVER);
#else
	k_thread_join(&my_thread, K_FOREVER);
#endif
	printk("Test thread joined\n");
}
LL_EXTENSION_SYMBOL(test_entry);
