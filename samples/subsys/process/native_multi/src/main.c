/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file main.c
 * @brief Native multi-thread sample for the Zephyr Process Model.
 *
 * Demonstrates a process whose threads are ordinary functions linked into
 * the main Zephyr image (no LLEXT, no ELF loading).  Three worker threads
 * are grouped under a single process and joined together.
 *
 * Optionally enable @kconfig{CONFIG_PROCESS_FATAL_HANDLER} and set
 * @c opts.halt_on_fault to demonstrate the system-halt-on-fault policy.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/process/process.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define WORKER_STACK_SIZE 1024
#define WORKER_COUNT      3

Z_PROCESS_STACK_DEFINE(worker_stacks_0, WORKER_STACK_SIZE);
Z_PROCESS_STACK_DEFINE(worker_stacks_1, WORKER_STACK_SIZE);
Z_PROCESS_STACK_DEFINE(worker_stacks_2, WORKER_STACK_SIZE);

static k_thread_stack_t *worker_stacks[WORKER_COUNT] = {
	worker_stacks_0,
	worker_stacks_1,
	worker_stacks_2,
};

static struct z_process workers_proc;

static void worker_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uintptr_t id = (uintptr_t)p1;

	for (int i = 0; i < 3; i++) {
		LOG_INF("worker %u: iteration %d", (unsigned int)id, i);
		k_msleep(50);
	}
}

int main(void)
{
	LOG_INF("Process Model sample: native multi-thread");

	struct z_process_opts opts = Z_PROCESS_OPTS_DEFAULT;

	/*
	 * Enable system halt if any worker faults (requires
	 * CONFIG_PROCESS_FATAL_HANDLER=y, otherwise this flag is ignored).
	 */
	opts.halt_on_fault = false;

	int ret = z_process_init(&workers_proc, "workers", &opts);

	if (ret != 0) {
		LOG_ERR("z_process_init failed: %d", ret);
		return ret;
	}

	for (uintptr_t i = 0; i < WORKER_COUNT; i++) {
		ret = z_process_add_thread(&workers_proc, worker_stacks[i],
					   WORKER_STACK_SIZE,
					   worker_fn, (void *)i, NULL);
		if (ret != 0) {
			LOG_ERR("add_thread %u failed: %d",
				(unsigned int)i, ret);
			z_process_unload(&workers_proc);
			return ret;
		}
	}

	ret = z_process_start(&workers_proc);
	if (ret != 0) {
		LOG_ERR("z_process_start failed: %d", ret);
		z_process_unload(&workers_proc);
		return ret;
	}

	/* Wait for ALL worker threads to finish. */
	ret = z_process_join(&workers_proc, K_SECONDS(5));
	if (ret != 0) {
		LOG_WRN("join timed out (%d); killing remaining threads", ret);
		z_process_kill(&workers_proc);
	}

	LOG_INF("All workers done");
	z_process_unload(&workers_proc);
	return 0;
}
