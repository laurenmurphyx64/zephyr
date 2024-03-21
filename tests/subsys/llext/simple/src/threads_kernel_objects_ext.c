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

#define STACK_SIZE 1024
static struct k_thread my_thread;
static K_THREAD_STACK_DEFINE(my_thread_stack, STACK_SIZE);

struct k_sem my_sem;

/* Deleting exactly 1 character from any of the test_thread printk
 * statements OR the 2 printk statements before the last
 * in test_entry will cause the test to pass.
 */

void test_thread(void *arg0, void *arg1, void *arg2)
{
	// printk("Thread printk 1\n"); 
	// printk("Thread printk 2\n");
	// printk("Thread printk 3\n");
	// printk("Take semaphore from test thread\n");
	k_sem_take(&my_sem, K_FOREVER);
	// printk("Semaphore taken\n");
}

void test_entry(void)
{
	// printk("Main printk 1\n");
	// printk("Main printk 2\n"); // 2
	// printk("Main printk 3\n"); // ain printk 3
	// printk("Initialize semaphore with initial count 0 and max 1\n");
	k_sem_init(&my_sem, 1, 1);
	// printk("Give semaphore from main thread\n");
	k_sem_give(&my_sem);
	
	// printk("Creating thread\n");
	k_tid_t tid = k_thread_create(&my_thread, my_thread_stack, STACK_SIZE,
				&test_thread, NULL, NULL, NULL,
				0, 0, K_FOREVER);
	// printk("Starting thread\n");
	k_thread_start(tid);
	k_thread_join(&my_thread, K_FOREVER);
	// printk("Thread joined\n");
}
LL_EXTENSION_SYMBOL(test_entry);
