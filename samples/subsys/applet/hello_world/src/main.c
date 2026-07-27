/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file main.c
 * @brief Hello World sample for the Zephyr Applet subsystem
 *
 * Demonstrates the simplest possible use of the applet API:
 *
 *   1. z_applet_spawn() – load the ELF and start the thread in one call
 *   2. z_applet_join()  – wait for the applet to finish
 *   3. z_applet_unload() – release all resources
 *
 * The loadable extension (hello_ext.c) is compiled into hello.llext at build
 * time and embedded as a C array via hello.inc.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/applet/applet.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#ifdef CONFIG_LLEXT
/* The extension binary is embedded as a C array by the build system. */
static const uint8_t hello_world_elf[] __aligned(4096) = {
#include <hello_world.inc>
};
#else
extern void applet_main(void *arg);
#endif /* CONFIG_LLEXT */

/* Stack for the applet thread */
Z_APPLET_THREAD_STACK_DEFINE(hello_world_stack, CONFIG_APPLET_THREAD_STACK_SIZE_DEFAULT);

/* Applet descriptor */
static struct z_applet hello_world_applet;

int main(void)
{
	LOG_INF("Applet sample: hello world");

	/*
	 * Configure applet options.
	 * Pass the integer 42 as the argument so the extension can print it.
	 */
	struct z_applet_opts opts = Z_APPLET_OPTS_DEFAULT;

	opts.arg = (void *)(uintptr_t)42;

	/*
	 * Load and start the applet in one call.
	 * The extension runs in user mode (hardware-isolated) when
	 * userspace is enabled.
	 */
	int ret;
#ifdef CONFIG_LLEXT
	ret = z_applet_spawn(&hello_world_applet, "hello world llext",
			     hello_world_elf, sizeof(hello_world_elf),
			     hello_world_stack, sizeof(hello_world_stack),
			     &opts);
	if (ret != 0) {
		LOG_ERR("z_applet_spawn failed: %d", ret);
		return ret;
	}
#else
	ret = z_applet_init(&hello_world_applet, "hello world native", &opts);
	if (ret != 0) {
		LOG_ERR("z_applet_init failed: %d", ret);
		return ret;
	}

	ret = z_applet_add_thread(&hello_world_applet, hello_world_stack,
			    sizeof(hello_world_stack), (k_thread_entry_t) applet_main,
			    opts.arg, NULL);
	if (ret != 0) {
		LOG_ERR("z_applet_add_thread failed: %d", ret);
		return ret;
	}

	ret = z_applet_start(&hello_world_applet);
	if (ret != 0) {
		LOG_ERR("z_applet_start failed: %d", ret);
		return ret;
	}
#endif

	/* Wait for the applet to finish */
	ret = z_applet_join(&hello_world_applet, K_SECONDS(5));
	if (ret != 0) {
		LOG_ERR("z_applet_join failed: %d", ret);
		z_applet_kill(&hello_world_applet);
	} else {
		LOG_INF("Applet exited with code %d",
			z_applet_exit_code(&hello_world_applet));
	}

	/* Release LLEXT memory and reset the descriptor */
	z_applet_unload(&hello_world_applet);

	LOG_INF("Done");
	return 0;
}
