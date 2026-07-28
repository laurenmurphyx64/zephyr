/*
 * Copyright (c) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/arch/cpu.h>

/**
 * @brief Extension entry point
 *
 * Called by the applet subsystem after the extension is loaded and its
 * initialisation functions (.init_array) have run.
 *
 * @param arg  Opaque argument forwarded from z_applet_opts.arg; the host
 *             application passes the integer 42 cast to void*.
 */
void applet_main(void *arg)
{
	unsigned int cpu = arch_curr_cpu()->id;
	printk("applet main thread %p on CPU %u\n", k_current_get(), cpu);
}

LL_EXTENSION_SYMBOL(applet_main);
