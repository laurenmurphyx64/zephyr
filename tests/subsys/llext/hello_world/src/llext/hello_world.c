/*
 * Copyright (c) 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This very simple hello world C code can be used as a test case for building
 * probably the simplest loadable extension. It requires a single symbol be
 * linked, section relocation support, and the ability to export and call out to
 * a function.
 */

#include <stdint.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/kernel.h>

static const uint32_t number = 42;

#define STACK_SIZE 1024
static K_THREAD_STACK_DEFINE(dyn_thread_stack, STACK_SIZE);
static struct k_thread dyn_thread; /* */

/*
static struct k_mutex my_mutex;
*/

static struct k_sem my_sem;

/*
static const uint32_t number = 42;
*/

void thread_test() {
	k_sem_take(&my_sem, K_FOREVER);
	printk("Thread test");
}

void hello_world(void)
{
	/*
	printk("hello world\n");
	printk("A number is %u\n", number);

	method_test();
	k_mutex_init(&my_mutex);
	k_mutex_lock(&my_mutex, K_FOREVER);
	k_mutex_unlock(&my_mutex);
	*/

	printk("Initialize sem\n");
	k_sem_init(&my_sem, 0, 1);
	printk("Give sem\n");
	k_sem_give(&my_sem);

	printk("Allocating thread\n");
	// dyn_thread = k_object_alloc(K_OBJ_THREAD);
	printk("Thread: %x\n", dyn_thread);
	printk("Creating thread\n");
	k_tid_t tid = k_thread_create(&dyn_thread, dyn_thread_stack, STACK_SIZE,
				thread_test, NULL, NULL, NULL,
				0, 0, K_FOREVER);
	printk("Starting thread\n");
	k_thread_start(tid);
}
LL_EXTENSION_SYMBOL(hello_world);
