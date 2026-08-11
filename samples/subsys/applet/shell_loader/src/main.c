/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file main.c
 * @brief Shell front-end for loading and running LLEXT-backed applets.
 *
 * Mirrors the LLEXT shell loader sample, but every extension is wrapped in
 * an applet, so the ELF is loaded, given a thread and (when
 * CONFIG_USERSPACE is enabled) placed in its own memory domain by the
 * applet subsystem instead of by this application.
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/applet/applet.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(applet_shell, CONFIG_APPLET_LOG_LEVEL);

/** Number of applets that can be loaded at the same time */
#define APPLET_SHELL_MAX_LOADED 4

/** Size of the temporary buffer an ELF is decoded into */
#define APPLET_SHELL_MAX_SIZE 8192

/** Default timeout of the "join" command, in milliseconds */
#define APPLET_SHELL_JOIN_TIMEOUT_MS 1000

#define APPLET_LIST_HELP "List loaded applets, their state and thread count"

#define APPLET_LOAD_HEX_HELP                                                                       \
	SHELL_HELP("Load an elf file encoded in hex directly from the shell input.",               \
		   "<applet_name> <ext_hex_string>")

#define APPLET_START_HELP                                                                          \
	SHELL_HELP("Start a loaded applet, running its " APPLET_ENTRY_SYM "() entry point.",       \
		   "<applet_name>")

#define APPLET_JOIN_HELP                                                                           \
	SHELL_HELP("Wait for every thread of an applet to finish.",                                \
		   "<applet_name> [timeout_ms]")

#define APPLET_KILL_HELP                                                                           \
	SHELL_HELP("Abort every running thread of an applet.",                                     \
		   "<applet_name>")

#define APPLET_UNLOAD_HELP                                                                         \
	SHELL_HELP("Unload an applet and release its ELF buffer.",                                 \
		   "<applet_name>")

K_THREAD_STACK_ARRAY_DEFINE(applet_stacks, APPLET_SHELL_MAX_LOADED,
			    CONFIG_APPLET_THREAD_STACK_SIZE_DEFAULT);

static struct applet_slot {
	struct applet inst;
	/* Decoded ELF image; must stay valid for the lifetime of the applet. */
	uint8_t *buf;
	char name[CONFIG_APPLET_NAME_MAX_LEN + 1];
	bool in_use;
} applet_slots[APPLET_SHELL_MAX_LOADED];

static const char *const applet_state_str[] = {
	[APPLET_STATE_UNLOADED] = "unloaded",
	[APPLET_STATE_LOADED]   = "loaded",
	[APPLET_STATE_RUNNING]  = "running",
	[APPLET_STATE_DYING]    = "dying",
	[APPLET_STATE_DEAD]     = "dead",
};

static struct applet_slot *slot_by_name(const char *name)
{
	for (size_t i = 0; i < APPLET_SHELL_MAX_LOADED; i++) {
		if (applet_slots[i].in_use && strcmp(applet_slots[i].name, name) == 0) {
			return &applet_slots[i];
		}
	}
	return NULL;
}

static struct applet_slot *slot_alloc(size_t *idx)
{
	for (size_t i = 0; i < APPLET_SHELL_MAX_LOADED; i++) {
		if (!applet_slots[i].in_use) {
			*idx = i;
			return &applet_slots[i];
		}
	}
	return NULL;
}

static void slot_release(struct applet_slot *slot)
{
	free(slot->buf);
	slot->buf = NULL;
	slot->name[0] = '\0';
	slot->in_use = false;
}

static void applet_name_get(size_t idx, struct shell_static_entry *entry)
{
	size_t n = 0;

	entry->syntax = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
	entry->handler = NULL;
	entry->args.mandatory = 0;
	entry->args.optional = 0;

	for (size_t i = 0; i < APPLET_SHELL_MAX_LOADED; i++) {
		if (!applet_slots[i].in_use) {
			continue;
		}
		if (n == idx) {
			entry->syntax = applet_slots[i].name;
			return;
		}
		n++;
	}
}

SHELL_DYNAMIC_CMD_CREATE(msub_applet_name, applet_name_get);

static int cmd_applet_list(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "| Name             | State      | Threads |");
	for (size_t i = 0; i < APPLET_SHELL_MAX_LOADED; i++) {
		struct applet_slot *slot = &applet_slots[i];

		if (!slot->in_use) {
			continue;
		}

		shell_print(sh, "| %16s | %10s | %7u |", slot->name,
			    applet_state_str[applet_get_state(&slot->inst)],
			    applet_thread_count(&slot->inst));
	}

	return 0;
}

static int cmd_applet_load_hex(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);

	const char *name = argv[1];
	size_t hex_len = strlen(argv[2]);
	size_t idx;

	if (strlen(name) > CONFIG_APPLET_NAME_MAX_LEN) {
		shell_error(sh, "Applet name too long, max %d chars", CONFIG_APPLET_NAME_MAX_LEN);
		return -EINVAL;
	}

	if (hex_len > APPLET_SHELL_MAX_SIZE * 2) {
		shell_error(sh, "Extension %zu bytes too large to load, max %d bytes",
			    hex_len / 2, APPLET_SHELL_MAX_SIZE);
		return -ENOMEM;
	}

	if (slot_by_name(name) != NULL) {
		shell_error(sh, "Applet %s already loaded", name);
		return -EEXIST;
	}

	struct applet_slot *slot = slot_alloc(&idx);

	if (slot == NULL) {
		shell_error(sh, "Too many applets loaded, max %d; unload one first",
			    APPLET_SHELL_MAX_LOADED);
		return -ENOMEM;
	}

	slot->buf = aligned_alloc(Z_KERNEL_STACK_OBJ_ALIGN, APPLET_SHELL_MAX_SIZE);
	if (slot->buf == NULL) {
		shell_error(sh, "Failed to allocate %d byte load buffer", APPLET_SHELL_MAX_SIZE);
		return -ENOMEM;
	}

	size_t elf_len = hex2bin(argv[2], hex_len, slot->buf, APPLET_SHELL_MAX_SIZE);

	if (elf_len == 0) {
		shell_error(sh, "Invalid hex input for applet %s", name);
		free(slot->buf);
		slot->buf = NULL;
		return -EINVAL;
	}

	LOG_DBG("hex len %zu, buf size %d, decoded %zu bytes", hex_len, APPLET_SHELL_MAX_SIZE,
		elf_len);

	struct applet_opts opts = APPLET_OPTS_DEFAULT;
	int ret = applet_load(&slot->inst, name, slot->buf, elf_len, applet_stacks[idx],
			      K_THREAD_STACK_SIZEOF(applet_stacks[idx]), &opts);

	if (ret != 0) {
		shell_error(sh, "Failed to load applet %s, return code %d", name, ret);
		free(slot->buf);
		slot->buf = NULL;
		return ret;
	}

	strncpy(slot->name, name, CONFIG_APPLET_NAME_MAX_LEN);
	slot->name[CONFIG_APPLET_NAME_MAX_LEN] = '\0';
	slot->in_use = true;

	shell_print(sh, "Successfully loaded applet %s (%zu bytes)", slot->name, elf_len);
	return 0;
}

static int cmd_applet_start(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);

	struct applet_slot *slot = slot_by_name(argv[1]);

	if (slot == NULL) {
		shell_error(sh, "No such applet %s", argv[1]);
		return -ENOENT;
	}

	int ret = applet_start(&slot->inst);

	if (ret != 0) {
		shell_error(sh, "Failed to start applet %s, return code %d", argv[1], ret);
		return ret;
	}

	shell_print(sh, "Started applet %s", argv[1]);
	return 0;
}

static int cmd_applet_join(const struct shell *sh, size_t argc, char *argv[])
{
	struct applet_slot *slot = slot_by_name(argv[1]);

	if (slot == NULL) {
		shell_error(sh, "No such applet %s", argv[1]);
		return -ENOENT;
	}

	uint32_t timeout_ms = APPLET_SHELL_JOIN_TIMEOUT_MS;

	if (argc > 2) {
		timeout_ms = (uint32_t)strtoul(argv[2], NULL, 0);
	}

	int ret = applet_join(&slot->inst, K_MSEC(timeout_ms));

	if (ret != 0) {
		shell_error(sh, "Applet %s did not finish, return code %d", argv[1], ret);
		return ret;
	}

	shell_print(sh, "Applet %s finished with exit code %d", argv[1],
		    applet_exit_code(&slot->inst));
	return 0;
}

static int cmd_applet_kill(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);

	struct applet_slot *slot = slot_by_name(argv[1]);

	if (slot == NULL) {
		shell_error(sh, "No such applet %s", argv[1]);
		return -ENOENT;
	}

	int ret = applet_kill(&slot->inst);

	if (ret != 0) {
		shell_error(sh, "Failed to kill applet %s, return code %d", argv[1], ret);
		return ret;
	}

	shell_print(sh, "Killed applet %s", argv[1]);
	return 0;
}

static int cmd_applet_unload(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);

	struct applet_slot *slot = slot_by_name(argv[1]);

	if (slot == NULL) {
		shell_error(sh, "No such applet %s", argv[1]);
		return -ENOENT;
	}

	applet_unload(&slot->inst);
	slot_release(slot);

	shell_print(sh, "Unloaded applet %s", argv[1]);
	return 0;
}

/* clang-format off */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_applet,
	SHELL_CMD(list, NULL, APPLET_LIST_HELP, cmd_applet_list),
	SHELL_CMD_ARG(load_hex, NULL, APPLET_LOAD_HEX_HELP, cmd_applet_load_hex, 3, 0),
	SHELL_CMD_ARG(start, &msub_applet_name, APPLET_START_HELP, cmd_applet_start, 2, 0),
	SHELL_CMD_ARG(join, &msub_applet_name, APPLET_JOIN_HELP, cmd_applet_join, 2, 1),
	SHELL_CMD_ARG(kill, &msub_applet_name, APPLET_KILL_HELP, cmd_applet_kill, 2, 0),
	SHELL_CMD_ARG(unload, &msub_applet_name, APPLET_UNLOAD_HELP, cmd_applet_unload, 2, 0),

	SHELL_SUBCMD_SET_END
	);
/* clang-format on */

SHELL_CMD_REGISTER(applet, &sub_applet, "Applet commands", NULL);

int main(void)
{
	return 0;
}
