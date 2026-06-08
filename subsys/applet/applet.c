/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/applet/applet.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/libc-hooks.h>

#ifdef CONFIG_APPLET_LLEXT
#include <zephyr/llext/buf_loader.h>
#include <zephyr/llext/llext.h>
#endif

#ifdef CONFIG_APPLET_FATAL_HANDLER
#include <zephyr/fatal.h>
#endif

LOG_MODULE_REGISTER(applet, CONFIG_APPLET_LOG_LEVEL);

/* -------------------------------------------------------------------------
 * Per-thread slot allocator (private k_heap)
 * -----------------------------------------------------------------------*/

K_HEAP_DEFINE(applet_slot_heap, CONFIG_APPLET_HEAP_SIZE);

static struct z_applet_thread *slot_alloc(void)
{
	struct z_applet_thread *slot =
		k_heap_alloc(&applet_slot_heap, sizeof(*slot), K_NO_WAIT);
	if (slot == NULL) {
		return NULL;
	}

	memset(slot, 0, sizeof(*slot));

#ifdef CONFIG_USERSPACE
	slot->thread = k_object_alloc(K_OBJ_THREAD);
#else
	slot->thread = k_heap_alloc(&applet_slot_heap, sizeof(struct k_thread), K_NO_WAIT);
#endif

	if (slot->thread == NULL) {
		k_heap_free(&applet_slot_heap, slot);
		return NULL;
	}

	return slot;
}

static void slot_free(struct z_applet_thread *slot)
{
	if (slot->thread != NULL) {
#ifdef CONFIG_USERSPACE
		k_object_free(slot->thread);
#else
		k_heap_free(&applet_slot_heap, slot->thread);
#endif
		slot->thread = NULL;
	}
	k_heap_free(&applet_slot_heap, slot);
}

/* -------------------------------------------------------------------------
 * Global applet registry (used by the fatal handler)
 * -----------------------------------------------------------------------*/

static sys_slist_t applet_list = SYS_SLIST_STATIC_INIT(&applet_list);
static struct k_spinlock applet_list_lock;

static void applet_register(struct z_applet *applet)
{
	k_spinlock_key_t key = k_spin_lock(&applet_list_lock);

	sys_slist_append(&applet_list, &applet->reg_node);
	k_spin_unlock(&applet_list_lock, key);
}

static void applet_unregister(struct z_applet *applet)
{
	k_spinlock_key_t key = k_spin_lock(&applet_list_lock);

	(void)sys_slist_find_and_remove(&applet_list, &applet->reg_node);
	k_spin_unlock(&applet_list_lock, key);
}

/* -------------------------------------------------------------------------
 * Internal helpers
 * -----------------------------------------------------------------------*/

static void apply_default_opts(struct z_applet_opts *o)
{
	if (o->thread_stack_size == 0) {
		o->thread_stack_size = CONFIG_APPLET_THREAD_STACK_SIZE_DEFAULT;
	}
	if (o->entry_sym == NULL) {
		o->entry_sym = Z_APPLET_ENTRY_SYM;
	}
}

static int init_descriptor(struct z_applet *applet, const char *name,
			   const struct z_applet_opts *opts,
			   enum z_applet_kind kind)
{
	memset(applet, 0, sizeof(*applet));

	strncpy(applet->name, name, CONFIG_APPLET_NAME_MAX_LEN);
	applet->name[CONFIG_APPLET_NAME_MAX_LEN] = '\0';

	if (opts != NULL) {
		applet->opts = *opts;
	} else {
		struct z_applet_opts defaults = Z_APPLET_OPTS_DEFAULT;

		applet->opts = defaults;
	}
	apply_default_opts(&applet->opts);

	applet->kind = kind;
	sys_slist_init(&applet->threads);

#ifdef CONFIG_USERSPACE
	int ret = k_mem_domain_init(&applet->domain, 0, NULL);

	if (ret != 0) {
		LOG_ERR("applet '%s': k_mem_domain_init failed (%d)",
			applet->name, ret);
		return ret;
	}
	applet->has_domain = true;
#endif

	applet->state = Z_APPLET_STATE_LOADED;
	applet_register(applet);
	return 0;
}

/* -------------------------------------------------------------------------
 * Public API — construction
 * -----------------------------------------------------------------------*/

int z_applet_init(struct z_applet *applet, const char *name,
		   const struct z_applet_opts *opts)
{
	if (applet == NULL || name == NULL) {
		return -EINVAL;
	}

	int ret = init_descriptor(applet, name, opts, Z_APPLET_KIND_NATIVE);

	if (ret != 0) {
		return ret;
	}

	/*
	 * Native applets default to supervisor mode: their entry functions
	 * are arbitrary C code linked into the main image and usually call
	 * APIs that are not user-callable. Callers who really want native
	 * user-mode threads must opt in explicitly via opts.user_mode.
	 */
	if (opts == NULL) {
		applet->opts.user_mode = false;
	}

	LOG_INF("applet '%s': initialised (native)", applet->name);
	return 0;
}

#ifdef CONFIG_APPLET_LLEXT
int z_applet_load_ext(struct z_applet *applet, const char *name,
		       const void *elf_data, size_t elf_size,
		       const struct z_applet_opts *opts)
{
	if (applet == NULL || name == NULL || elf_data == NULL || elf_size == 0) {
		return -EINVAL;
	}

	int ret = init_descriptor(applet, name, opts, Z_APPLET_KIND_LLEXT);

	if (ret != 0) {
		return ret;
	}

	struct llext_buf_loader buf_loader =
		LLEXT_BUF_LOADER((const uint8_t *)elf_data, elf_size);
	const struct llext_load_param ldr_parm = LLEXT_LOAD_PARAM_DEFAULT;

	ret = llext_load(&buf_loader.loader, applet->name, &applet->ext, &ldr_parm);
	if (ret != 0) {
		LOG_ERR("applet '%s': llext_load failed (%d)", applet->name, ret);
		goto err;
	}

#ifdef Z_LIBC_PARTITION_EXISTS
	z_applet_add_partition(applet, &z_libc_partition);
#endif

#ifdef CONFIG_USERSPACE
	ret = llext_add_domain(applet->ext, &applet->domain);
	if (ret != 0) {
		LOG_ERR("applet '%s': llext_add_domain failed (%d)",
			applet->name, ret);
		llext_unload(&applet->ext);
		goto err;
	}
#endif /* CONFIG_USERSPACE */

	LOG_INF("applet '%s': loaded (LLEXT)", applet->name);
	return 0;

err:
#ifdef CONFIG_USERSPACE
	if (applet->has_domain) {
		k_mem_domain_deinit(&applet->domain);
		applet->has_domain = false;
	}
#endif
	applet_unregister(applet);
	applet->state = Z_APPLET_STATE_UNLOADED;
	return ret;
}
#endif /* CONFIG_APPLET_LLEXT */

int z_applet_add_partition(struct z_applet *applet,
			    struct k_mem_partition *part)
{
	if (applet == NULL || part == NULL) {
		return -EINVAL;
	}
	if (applet->state == Z_APPLET_STATE_UNLOADED) {
		return -EINVAL;
	}

#ifdef CONFIG_USERSPACE
	if (!applet->has_domain) {
		return -EINVAL;
	}
	return k_mem_domain_add_partition(&applet->domain, part);
#else
	ARG_UNUSED(part);
	return 0;
#endif
}

/* -------------------------------------------------------------------------
 * Public API — thread attachment
 * -----------------------------------------------------------------------*/

static int add_thread_internal(struct z_applet *applet,
			       k_thread_stack_t *stack, size_t stack_size,
			       k_thread_entry_t entry, void *arg,
			       const char *thread_name)
{
	if (applet == NULL || stack == NULL || stack_size == 0 || entry == NULL) {
		return -EINVAL;
	}
	if (applet->state != Z_APPLET_STATE_LOADED) {
		return -EINVAL;
	}

	struct z_applet_thread *slot = slot_alloc();

	if (slot == NULL) {
		LOG_ERR("applet '%s': unable to allocate slot from heaps"
			"(CONFIG_APPLET_HEAP_SIZE=%d, CONFIG_HEAP_MEM_POOL_SIZE=%d)",
			applet->name, CONFIG_APPLET_HEAP_SIZE, CONFIG_HEAP_MEM_POOL_SIZE);
		return -ENOMEM;
	}

	slot->stack      = stack;
	slot->stack_size = stack_size;
	slot->entry_fn   = entry;
	slot->arg        = arg;
	slot->priority   = applet->opts.thread_priority;

	uint32_t opts_flags = 0;

#ifdef CONFIG_USERSPACE
	if (applet->opts.user_mode && applet->has_domain) {
		opts_flags |= K_USER;
	}
#endif

#ifdef CONFIG_APPLET_LLEXT
	k_thread_create(slot->thread, slot->stack, slot->stack_size,
			(k_thread_entry_t) &llext_bootstrap,
			applet->ext, slot->entry_fn, slot->arg,
			slot->priority, opts_flags, K_FOREVER);
#else
	k_thread_create(slot->thread, slot->stack, slot->stack_size,
			slot->entry_fn, slot->arg, NULL, NULL,
			slot->priority, opts_flags, K_FOREVER);
#endif
	LOG_DBG("applet '%s': added thread %p (entry=%p, arg=%p, stack=%p, size=%zu)",
		applet->name, slot->thread, slot->entry_fn, slot->arg,
		slot->stack, slot->stack_size);

	k_thread_name_set(slot->thread,
			  thread_name != NULL ? thread_name : applet->name);

	sys_slist_append(&applet->threads, &slot->node);
	applet->thread_count++;
	return 0;
}

int z_applet_add_thread(struct z_applet *applet,
			 k_thread_stack_t *stack, size_t stack_size,
			 k_thread_entry_t entry, void *arg,
			 const char *thread_name)
{
	return add_thread_internal(applet, stack, stack_size, entry, arg,
				   thread_name);
}

#ifdef CONFIG_APPLET_LLEXT
int z_applet_add_thread_sym(struct z_applet *applet,
			     k_thread_stack_t *stack, size_t stack_size,
			     const char *entry_sym, void *arg,
			     const char *thread_name)
{
	if (applet == NULL || applet->kind != Z_APPLET_KIND_LLEXT) {
		return -EINVAL;
	}

	if (entry_sym == NULL) {
		entry_sym = applet->opts.entry_sym;
	}
	if (entry_sym == NULL) {
		entry_sym = Z_APPLET_ENTRY_SYM;
	}

	llext_entry_fn_t fn = (llext_entry_fn_t)llext_find_sym(
		&applet->ext->exp_tab, entry_sym);

	if (fn == NULL) {
		LOG_ERR("applet '%s': entry symbol '%s' not found",
			applet->name, entry_sym);
		return -ENOENT;
	}

	return add_thread_internal(applet, stack, stack_size,
				   (k_thread_entry_t)fn, arg, thread_name);
}
#endif /* CONFIG_APPLET_LLEXT */

/* -------------------------------------------------------------------------
 * Public API — execution control
 * -----------------------------------------------------------------------*/

int z_applet_start(struct z_applet *applet)
{
	if (applet == NULL || applet->state != Z_APPLET_STATE_LOADED) {
		return -EINVAL;
	}
	if (applet->thread_count == 0) {
		return -EINVAL;
	}

#ifdef CONFIG_APPLET_LLEXT
	if (applet->kind == Z_APPLET_KIND_LLEXT && !applet->bringup_done) {
		int ret = llext_bringup(applet->ext);

		if (ret != 0) {
			LOG_ERR("applet '%s': llext_bringup failed (%d)",
				applet->name, ret);
			applet->exit_code = ret;
			applet->state = Z_APPLET_STATE_DEAD;
			return ret;
		}
		applet->bringup_done = true;
	}
#endif

	struct z_applet_thread *slot;

	SYS_SLIST_FOR_EACH_CONTAINER(&applet->threads, slot, node) {
		if (slot->started) {
			continue;
		}

#ifdef CONFIG_USERSPACE
		if (applet->has_domain) {
			if (applet->opts.share_stacks && !slot->stack_part_added) {
				slot->stack_part.start = (uintptr_t)
					K_THREAD_STACK_BUFFER(slot->stack);
				slot->stack_part.size = slot->stack_size;
				slot->stack_part.attr = K_MEM_PARTITION_P_RW_U_RW;
				int pret = z_applet_add_partition(
					applet, &slot->stack_part);
				if (pret == 0) {
					slot->stack_part_added = true;
				} else {
					LOG_WRN("applet '%s': add stack partition "
						"failed (%d)",
						applet->name, pret);
				}
			}
			k_mem_domain_add_thread(&applet->domain, slot->thread);
		}
#endif

		slot->started = true;
		k_thread_start(slot->thread);
	}

	applet->state = Z_APPLET_STATE_RUNNING;
	LOG_DBG("applet '%s': started (%u thread%s)",
		applet->name, applet->thread_count,
		applet->thread_count == 1 ? "" : "s");
	return 0;
}

#ifdef CONFIG_APPLET_LLEXT
int z_applet_load(struct z_applet *applet, const char *name,
		   const void *elf_data, size_t elf_size,
		   k_thread_stack_t *stack, size_t stack_size,
		   const struct z_applet_opts *opts)
{
	int ret = z_applet_load_ext(applet, name, elf_data, elf_size, opts);

	if (ret != 0) {
		return ret;
	}

	ret = z_applet_add_thread_sym(applet, stack, stack_size,
				       applet->opts.entry_sym, applet->opts.arg,
				       NULL);
	if (ret != 0) {
		z_applet_unload(applet);
		return ret;
	}
	return 0;
}

int z_applet_spawn(struct z_applet *applet, const char *name,
		    const void *elf_data, size_t elf_size,
		    k_thread_stack_t *stack, size_t stack_size,
		    const struct z_applet_opts *opts)
{
	int ret = z_applet_load(applet, name, elf_data, elf_size, stack,
				 stack_size, opts);
	if (ret != 0) {
		return ret;
	}
	return z_applet_start(applet);
}
#endif /* CONFIG_APPLET_LLEXT */

int z_applet_join(struct z_applet *applet, k_timeout_t timeout)
{
	if (applet == NULL) {
		return -EINVAL;
	}
	if (applet->state != Z_APPLET_STATE_RUNNING &&
	    applet->state != Z_APPLET_STATE_DEAD) {
		return -EINVAL;
	}
	if (applet->state == Z_APPLET_STATE_DEAD) {
		return 0;
	}

	struct z_applet_thread *slot;

	SYS_SLIST_FOR_EACH_CONTAINER(&applet->threads, slot, node) {
		if (!slot->started || slot->joined) {
			continue;
		}

		int ret = k_thread_join(slot->thread, timeout);

		if (ret != 0) {
			return ret;
		}
		slot->joined = true;
	}

	applet->state = Z_APPLET_STATE_DEAD;
	return 0;
}

int z_applet_kill(struct z_applet *applet)
{
	if (applet == NULL || applet->state != Z_APPLET_STATE_RUNNING) {
		return -EINVAL;
	}

	struct z_applet_thread *slot;

	SYS_SLIST_FOR_EACH_CONTAINER(&applet->threads, slot, node) {
		if (!slot->started || slot->joined) {
			continue;
		}
		k_thread_abort(slot->thread);
		slot->joined = true;
	}

	applet->exit_code = -ECANCELED;
	applet->state     = Z_APPLET_STATE_DEAD;

	LOG_INF("applet '%s': killed", applet->name);
	return 0;
}

void z_applet_unload(struct z_applet *applet)
{
	if (applet == NULL || applet->state == Z_APPLET_STATE_UNLOADED) {
		return;
	}

	if (applet->state == Z_APPLET_STATE_RUNNING) {
		LOG_WRN("applet '%s': unloading while still running; "
			"call z_applet_kill() first",
			applet->name);
		z_applet_kill(applet);
	}

#ifdef CONFIG_APPLET_LLEXT
	if (applet->kind == Z_APPLET_KIND_LLEXT && applet->ext != NULL) {
		int ret = llext_teardown(applet->ext);

		if (ret != 0) {
			LOG_WRN("applet '%s': llext_teardown failed (%d)",
				applet->name, ret);
		}
	}
#endif

#ifdef CONFIG_USERSPACE
	if (applet->has_domain) {
		k_mem_domain_deinit(&applet->domain);
	}
#endif

#ifdef CONFIG_APPLET_LLEXT
	if (applet->kind == Z_APPLET_KIND_LLEXT && applet->ext != NULL) {
		llext_unload(&applet->ext);
	}
#endif

	/* Free every per-thread slot */
	sys_snode_t *node;

	while ((node = sys_slist_get(&applet->threads)) != NULL) {
		struct z_applet_thread *slot =
			CONTAINER_OF(node, struct z_applet_thread, node);
		slot_free(slot);
	}

	applet_unregister(applet);
	LOG_INF("applet '%s': unloaded", applet->name);

	memset(applet, 0, sizeof(*applet));
	applet->state = Z_APPLET_STATE_UNLOADED;
}

/* -------------------------------------------------------------------------
 * Introspection
 * -----------------------------------------------------------------------*/

struct k_thread *z_applet_thread_get(struct z_applet *applet, unsigned int idx)
{
	if (applet == NULL) {
		return NULL;
	}

	struct z_applet_thread *slot;
	unsigned int i = 0;

	SYS_SLIST_FOR_EACH_CONTAINER(&applet->threads, slot, node) {
		if (i == idx) {
			return slot->thread;
		}
		i++;
	}
	return NULL;
}

/* -------------------------------------------------------------------------
 * Fatal handler
 * -----------------------------------------------------------------------*/

#ifdef CONFIG_APPLET_FATAL_HANDLER

static struct z_applet *find_applet_of_thread(struct k_thread *thread)
{
	struct z_applet *applet;

	SYS_SLIST_FOR_EACH_CONTAINER(&applet_list, applet, reg_node) {
		struct z_applet_thread *slot;

		SYS_SLIST_FOR_EACH_CONTAINER(&applet->threads, slot, node) {
			if (slot->thread == thread) {
				return applet;
			}
		}
	}
	return NULL;
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	struct k_thread *cur = k_current_get();
	struct z_applet *applet = find_applet_of_thread(cur);

	if (applet != NULL) {
		if (applet->opts.halt_on_fault) {
			LOG_PANIC();
			LOG_ERR("applet '%s': fatal error %u in thread %p; "
				"halting system",
				applet->name, reason, (void *)cur);
			k_fatal_halt(reason);
			CODE_UNREACHABLE;
		}

		LOG_ERR("applet '%s': fatal error %u in thread %p; "
			"aborting thread",
			applet->name, reason, (void *)cur);
		return;
	}

	LOG_PANIC();
	LOG_ERR("Halting system");
	k_fatal_halt(reason);
	CODE_UNREACHABLE;
}

#endif /* CONFIG_APPLET_FATAL_HANDLER */
