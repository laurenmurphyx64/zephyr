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

static struct k_thread my_thread;
#define STACK_SIZE 1024
extern void *my_thread_stack;
struct k_sem my_sem;

void test_thread(void *arg0, void *arg1, void *arg2)
{
	printk("Take semaphore from test thread\n");
	k_sem_take(&my_sem, K_FOREVER);
	printk("Semaphore taken\n");
}

void test_entry(void)
{
	printk("Initialize semaphore with initial count 0 and max 1\n");
	k_sem_init(&my_sem, 1, 1);
	printk("Give semaphore from main thread\n");
	k_sem_give(&my_sem);
	
	printk("Creating thread\n");
	k_tid_t tid = k_thread_create(&my_thread, my_thread_stack, STACK_SIZE,
				&test_thread, NULL, NULL, NULL,
				0, 0, K_FOREVER);
	printk("Starting thread\n");
	k_thread_start(tid);
	k_thread_join(&my_thread, K_FOREVER);
	printk("Thread joined\n");
}
LL_EXTENSION_SYMBOL(test_entry);
