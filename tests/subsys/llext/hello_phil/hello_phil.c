/*
 * Copyright (c) 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/kernel.h>

static const uint32_t number = 42;

// #define STACK_SIZE (2048)

// static struct k_thread thread;
// static K_THREAD_STACK_DEFINE(stack, STACK_SIZE);

// K_SEM_DEFINE(sem, 0, 5);

// K_MUTEX_DEFINE(mutex);

void philosopher(void *id, void *unused1, void *unused2)
{
	printk("hello phil %d\n", id);

	// k_sem_take(&sem, K_FOREVER);
	// k_mutex_lock(&mutex, K_NO_WAIT);

	// k_sem_give(&sem);
	// k_mutex_unlock(&mutex);
}

void hello_phil(void)
{
	printk("hello world\n");
	printk("A number is %lu\n", number);

	// k_thread_create(&thread, stack, STACK_SIZE,
	// 			philosopher, INT_TO_POINTER(0), NULL, NULL,
	// 			0, 0, K_FOREVER);

	philosopher(0, NULL, NULL);
}
LL_EXTENSION_SYMBOL(hello_phil);