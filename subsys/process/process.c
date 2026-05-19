/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/process/process.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_PROCESS_LLEXT
#include <zephyr/llext/buf_loader.h>
#include <zephyr/llext/llext.h>
#endif

#ifdef CONFIG_PROCESS_FATAL_HANDLER
#include <zephyr/fatal.h>
#endif

LOG_MODULE_REGISTER(process, CONFIG_PROCESS_LOG_LEVEL);

/* -------------------------------------------------------------------------
 * Per-thread slot allocator (private k_heap)
 * -----------------------------------------------------------------------*/

K_HEAP_DEFINE(process_slot_heap, CONFIG_PROCESS_HEAP_SIZE);

static struct z_process_thread *slot_alloc(void)
{
	struct z_process_thread *slot =
		k_heap_alloc(&process_slot_heap, sizeof(*slot), K_NO_WAIT);

	if (slot != NULL) {
		memset(slot, 0, sizeof(*slot));
	}
	return slot;
}

static void slot_free(struct z_process_thread *slot)
{
	k_heap_free(&process_slot_heap, slot);
}

/* -------------------------------------------------------------------------
 * Global process registry (used by the fatal handler)
 * -----------------------------------------------------------------------*/

static sys_slist_t process_list = SYS_SLIST_STATIC_INIT(&process_list);
static struct k_spinlock process_list_lock;

static void process_register(struct z_process *proc)
{
	k_spinlock_key_t key = k_spin_lock(&process_list_lock);

	sys_slist_append(&process_list, &proc->reg_node);
	k_spin_unlock(&process_list_lock, key);
}

static void process_unregister(struct z_process *proc)
{
	k_spinlock_key_t key = k_spin_lock(&process_list_lock);

	(void)sys_slist_find_and_remove(&process_list, &proc->reg_node);
	k_spin_unlock(&process_list_lock, key);
}

/* -------------------------------------------------------------------------
 * Thread trampolines
 * -----------------------------------------------------------------------*/

#ifdef CONFIG_USERSPACE
static FUNC_NORETURN void process_user_entry(void *p1, void *p2, void *p3)
{
	k_thread_entry_t fn = (k_thread_entry_t)p1;
	void *arg = p2;

	ARG_UNUSED(p3);

	fn(arg, NULL, NULL);

	k_thread_abort(k_current_get());
	CODE_UNREACHABLE;
}
#endif /* CONFIG_USERSPACE */

static void process_thread_trampoline(void *p1, void *p2, void *p3)
{
	struct z_process_thread *slot = (struct z_process_thread *)p1;
	struct z_process *proc = (struct z_process *)p2;

	ARG_UNUSED(p3);

	k_thread_entry_t fn = slot->entry_fn;
	void *arg = slot->arg;

#ifdef CONFIG_USERSPACE
	if (proc->opts.user_mode && proc->has_domain) {
		k_thread_user_mode_enter(process_user_entry,
					 (void *)fn, arg, NULL);
		CODE_UNREACHABLE;
	}
#else
	ARG_UNUSED(proc);
#endif

	fn(arg, NULL, NULL);
}

/* -------------------------------------------------------------------------
 * Internal helpers
 * -----------------------------------------------------------------------*/

static void apply_default_opts(struct z_process_opts *o)
{
	if (o->stack_size == 0) {
		o->stack_size = CONFIG_PROCESS_STACK_SIZE_DEFAULT;
	}
	if (o->entry_sym == NULL) {
		o->entry_sym = Z_PROCESS_ENTRY_SYM;
	}
}

static int init_descriptor(struct z_process *proc, const char *name,
			   const struct z_process_opts *opts,
			   enum z_process_kind kind)
{
	memset(proc, 0, sizeof(*proc));

	strncpy(proc->name, name, CONFIG_PROCESS_NAME_MAX_LEN);
	proc->name[CONFIG_PROCESS_NAME_MAX_LEN] = '\0';

	if (opts != NULL) {
		proc->opts = *opts;
	} else {
		struct z_process_opts defaults = Z_PROCESS_OPTS_DEFAULT;

		proc->opts = defaults;
	}
	apply_default_opts(&proc->opts);

	proc->kind = kind;
	sys_slist_init(&proc->threads);

#ifdef CONFIG_USERSPACE
	int ret = k_mem_domain_init(&proc->domain, 0, NULL);

	if (ret != 0) {
		LOG_ERR("process '%s': k_mem_domain_init failed (%d)",
			proc->name, ret);
		return ret;
	}
	proc->has_domain = true;
#endif

	proc->state = Z_PROCESS_STATE_LOADED;
	process_register(proc);
	return 0;
}

/* -------------------------------------------------------------------------
 * Public API — construction
 * -----------------------------------------------------------------------*/

int z_process_init(struct z_process *proc, const char *name,
		   const struct z_process_opts *opts)
{
	if (proc == NULL || name == NULL) {
		return -EINVAL;
	}

	int ret = init_descriptor(proc, name, opts, Z_PROCESS_KIND_NATIVE);

	if (ret != 0) {
		return ret;
	}

	/*
	 * Native processes default to supervisor mode: their entry functions
	 * are arbitrary C code linked into the main image and usually call
	 * APIs that are not user-callable.  Callers who really want native
	 * user-mode threads must opt in explicitly via opts.user_mode.
	 */
	if (opts == NULL) {
		proc->opts.user_mode = false;
	}

	LOG_INF("process '%s': initialised (native)", proc->name);
	return 0;
}

#ifdef CONFIG_PROCESS_LLEXT
int z_process_load_ext(struct z_process *proc, const char *name,
		       const void *elf_data, size_t elf_size,
		       const struct z_process_opts *opts)
{
	if (proc == NULL || name == NULL || elf_data == NULL || elf_size == 0) {
		return -EINVAL;
	}

	int ret = init_descriptor(proc, name, opts, Z_PROCESS_KIND_LLEXT);

	if (ret != 0) {
		return ret;
	}

	struct llext_buf_loader buf_loader =
		LLEXT_TEMPORARY_BUF_LOADER((const uint8_t *)elf_data, elf_size);
	const struct llext_load_param ldr_parm = LLEXT_LOAD_PARAM_DEFAULT;

	ret = llext_load(&buf_loader.loader, proc->name, &proc->ext, &ldr_parm);
	if (ret != 0) {
		LOG_ERR("process '%s': llext_load failed (%d)", proc->name, ret);
		goto err;
	}

#ifdef CONFIG_USERSPACE
	ret = llext_add_domain(proc->ext, &proc->domain);
	if (ret != 0) {
		LOG_ERR("process '%s': llext_add_domain failed (%d)",
			proc->name, ret);
		llext_unload(&proc->ext);
		goto err;
	}
#endif /* CONFIG_USERSPACE */

	LOG_INF("process '%s': loaded (LLEXT)", proc->name);
	return 0;

err:
#ifdef CONFIG_USERSPACE
	if (proc->has_domain) {
		k_mem_domain_deinit(&proc->domain);
		proc->has_domain = false;
	}
#endif
	process_unregister(proc);
	proc->state = Z_PROCESS_STATE_UNLOADED;
	return ret;
}
#endif /* CONFIG_PROCESS_LLEXT */

int z_process_add_partition(struct z_process *proc,
			    struct k_mem_partition *part)
{
	if (proc == NULL || part == NULL) {
		return -EINVAL;
	}
	if (proc->state == Z_PROCESS_STATE_UNLOADED) {
		return -EINVAL;
	}

#ifdef CONFIG_USERSPACE
	if (!proc->has_domain) {
		return -EINVAL;
	}
	return k_mem_domain_add_partition(&proc->domain, part);
#else
	ARG_UNUSED(part);
	return 0;
#endif
}

/* -------------------------------------------------------------------------
 * Public API — thread attachment
 * -----------------------------------------------------------------------*/

static int add_thread_internal(struct z_process *proc,
			       k_thread_stack_t *stack, size_t stack_size,
			       k_thread_entry_t entry, void *arg,
			       const char *thread_name)
{
	if (proc == NULL || stack == NULL || stack_size == 0 || entry == NULL) {
		return -EINVAL;
	}
	if (proc->state != Z_PROCESS_STATE_LOADED) {
		return -EINVAL;
	}

	struct z_process_thread *slot = slot_alloc();

	if (slot == NULL) {
		LOG_ERR("process '%s': out of slot heap "
			"(CONFIG_PROCESS_HEAP_SIZE=%d)",
			proc->name, CONFIG_PROCESS_HEAP_SIZE);
		return -ENOMEM;
	}

	slot->stack      = stack;
	slot->stack_size = stack_size;
	slot->entry_fn   = entry;
	slot->arg        = arg;
	slot->priority   = proc->opts.priority;

	uint32_t opts_flags = 0;

#ifdef CONFIG_USERSPACE
	if (proc->opts.user_mode && proc->has_domain) {
		opts_flags |= K_USER;
	}
#endif

	k_thread_create(&slot->thread, slot->stack, slot->stack_size,
			process_thread_trampoline, slot, proc, NULL,
			slot->priority, opts_flags, K_FOREVER);

	k_thread_name_set(&slot->thread,
			  thread_name != NULL ? thread_name : proc->name);

	sys_slist_append(&proc->threads, &slot->node);
	proc->thread_count++;
	return 0;
}

int z_process_add_thread(struct z_process *proc,
			 k_thread_stack_t *stack, size_t stack_size,
			 k_thread_entry_t entry, void *arg,
			 const char *thread_name)
{
	return add_thread_internal(proc, stack, stack_size, entry, arg,
				   thread_name);
}

#ifdef CONFIG_PROCESS_LLEXT
int z_process_add_thread_sym(struct z_process *proc,
			     k_thread_stack_t *stack, size_t stack_size,
			     const char *entry_sym, void *arg,
			     const char *thread_name)
{
	if (proc == NULL || proc->kind != Z_PROCESS_KIND_LLEXT) {
		return -EINVAL;
	}

	if (entry_sym == NULL) {
		entry_sym = proc->opts.entry_sym;
	}
	if (entry_sym == NULL) {
		entry_sym = Z_PROCESS_ENTRY_SYM;
	}

	llext_entry_fn_t fn = (llext_entry_fn_t)llext_find_sym(
		&proc->ext->exp_tab, entry_sym);

	if (fn == NULL) {
		LOG_ERR("process '%s': entry symbol '%s' not found",
			proc->name, entry_sym);
		return -ENOENT;
	}

	return add_thread_internal(proc, stack, stack_size,
				   (k_thread_entry_t)fn, arg, thread_name);
}
#endif /* CONFIG_PROCESS_LLEXT */

/* -------------------------------------------------------------------------
 * Public API — execution control
 * -----------------------------------------------------------------------*/

int z_process_start(struct z_process *proc)
{
	if (proc == NULL || proc->state != Z_PROCESS_STATE_LOADED) {
		return -EINVAL;
	}
	if (proc->thread_count == 0) {
		return -EINVAL;
	}

#ifdef CONFIG_PROCESS_LLEXT
	if (proc->kind == Z_PROCESS_KIND_LLEXT && !proc->bringup_done) {
		int ret = llext_bringup(proc->ext);

		if (ret != 0) {
			LOG_ERR("process '%s': llext_bringup failed (%d)",
				proc->name, ret);
			proc->exit_code = ret;
			proc->state = Z_PROCESS_STATE_DEAD;
			return ret;
		}
		proc->bringup_done = true;
	}
#endif

	struct z_process_thread *slot;

	SYS_SLIST_FOR_EACH_CONTAINER(&proc->threads, slot, node) {
		if (slot->started) {
			continue;
		}

#ifdef CONFIG_USERSPACE
		if (proc->has_domain) {
			if (proc->opts.share_stacks && !slot->stack_part_added) {
				slot->stack_part.start = (uintptr_t)
					K_THREAD_STACK_BUFFER(slot->stack);
				slot->stack_part.size = slot->stack_size;
				slot->stack_part.attr = K_MEM_PARTITION_P_RW_U_RW;
				int pret = k_mem_domain_add_partition(
					&proc->domain, &slot->stack_part);
				if (pret == 0) {
					slot->stack_part_added = true;
				} else {
					LOG_WRN("process '%s': add stack partition "
						"failed (%d)",
						proc->name, pret);
				}
			}
			k_mem_domain_add_thread(&proc->domain, &slot->thread);
		}
#endif

		slot->started = true;
		k_thread_start(&slot->thread);
	}

	proc->state = Z_PROCESS_STATE_RUNNING;
	LOG_DBG("process '%s': started (%u thread%s)",
		proc->name, proc->thread_count,
		proc->thread_count == 1 ? "" : "s");
	return 0;
}

#ifdef CONFIG_PROCESS_LLEXT
int z_process_load(struct z_process *proc, const char *name,
		   const void *elf_data, size_t elf_size,
		   k_thread_stack_t *stack, size_t stack_size,
		   const struct z_process_opts *opts)
{
	int ret = z_process_load_ext(proc, name, elf_data, elf_size, opts);

	if (ret != 0) {
		return ret;
	}

	ret = z_process_add_thread_sym(proc, stack, stack_size,
				       proc->opts.entry_sym, proc->opts.arg,
				       NULL);
	if (ret != 0) {
		z_process_unload(proc);
		return ret;
	}
	return 0;
}

int z_process_spawn(struct z_process *proc, const char *name,
		    const void *elf_data, size_t elf_size,
		    k_thread_stack_t *stack, size_t stack_size,
		    const struct z_process_opts *opts)
{
	int ret = z_process_load(proc, name, elf_data, elf_size, stack,
				 stack_size, opts);
	if (ret != 0) {
		return ret;
	}
	return z_process_start(proc);
}
#endif /* CONFIG_PROCESS_LLEXT */

int z_process_join(struct z_process *proc, k_timeout_t timeout)
{
	if (proc == NULL) {
		return -EINVAL;
	}
	if (proc->state != Z_PROCESS_STATE_RUNNING &&
	    proc->state != Z_PROCESS_STATE_DEAD) {
		return -EINVAL;
	}
	if (proc->state == Z_PROCESS_STATE_DEAD) {
		return 0;
	}

	struct z_process_thread *slot;

	SYS_SLIST_FOR_EACH_CONTAINER(&proc->threads, slot, node) {
		if (!slot->started || slot->joined) {
			continue;
		}

		int ret = k_thread_join(&slot->thread, timeout);

		if (ret != 0) {
			return ret;
		}
		slot->joined = true;
	}

	proc->state = Z_PROCESS_STATE_DEAD;
	return 0;
}

int z_process_kill(struct z_process *proc)
{
	if (proc == NULL || proc->state != Z_PROCESS_STATE_RUNNING) {
		return -EINVAL;
	}

	struct z_process_thread *slot;

	SYS_SLIST_FOR_EACH_CONTAINER(&proc->threads, slot, node) {
		if (!slot->started || slot->joined) {
			continue;
		}
		k_thread_abort(&slot->thread);
		slot->joined = true;
	}

	proc->exit_code = -ECANCELED;
	proc->state     = Z_PROCESS_STATE_DEAD;

	LOG_INF("process '%s': killed", proc->name);
	return 0;
}

void z_process_unload(struct z_process *proc)
{
	if (proc == NULL || proc->state == Z_PROCESS_STATE_UNLOADED) {
		return;
	}

	if (proc->state == Z_PROCESS_STATE_RUNNING) {
		LOG_WRN("process '%s': unloading while still running; "
			"call z_process_kill() first",
			proc->name);
		z_process_kill(proc);
	}

#ifdef CONFIG_PROCESS_LLEXT
	if (proc->kind == Z_PROCESS_KIND_LLEXT && proc->ext != NULL) {
		int ret = llext_teardown(proc->ext);

		if (ret != 0) {
			LOG_WRN("process '%s': llext_teardown failed (%d)",
				proc->name, ret);
		}
	}
#endif

#ifdef CONFIG_USERSPACE
	if (proc->has_domain) {
		k_mem_domain_deinit(&proc->domain);
	}
#endif

#ifdef CONFIG_PROCESS_LLEXT
	if (proc->kind == Z_PROCESS_KIND_LLEXT && proc->ext != NULL) {
		llext_unload(&proc->ext);
	}
#endif

	/* Free every per-thread slot */
	sys_snode_t *node;

	while ((node = sys_slist_get(&proc->threads)) != NULL) {
		struct z_process_thread *slot =
			CONTAINER_OF(node, struct z_process_thread, node);
		slot_free(slot);
	}

	process_unregister(proc);
	LOG_INF("process '%s': unloaded", proc->name);

	memset(proc, 0, sizeof(*proc));
	proc->state = Z_PROCESS_STATE_UNLOADED;
}

/* -------------------------------------------------------------------------
 * Introspection
 * -----------------------------------------------------------------------*/

struct k_thread *z_process_thread_get(struct z_process *proc, unsigned int idx)
{
	if (proc == NULL) {
		return NULL;
	}

	struct z_process_thread *slot;
	unsigned int i = 0;

	SYS_SLIST_FOR_EACH_CONTAINER(&proc->threads, slot, node) {
		if (i == idx) {
			return &slot->thread;
		}
		i++;
	}
	return NULL;
}

/* -------------------------------------------------------------------------
 * Fatal handler
 * -----------------------------------------------------------------------*/

#ifdef CONFIG_PROCESS_FATAL_HANDLER

static struct z_process *find_process_of_thread(struct k_thread *thread)
{
	struct z_process *proc;

	SYS_SLIST_FOR_EACH_CONTAINER(&process_list, proc, reg_node) {
		struct z_process_thread *slot;

		SYS_SLIST_FOR_EACH_CONTAINER(&proc->threads, slot, node) {
			if (&slot->thread == thread) {
				return proc;
			}
		}
	}
	return NULL;
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	struct k_thread *cur = k_current_get();
	struct z_process *proc = find_process_of_thread(cur);

	if (proc != NULL) {
		if (proc->opts.halt_on_fault) {
			LOG_PANIC();
			LOG_ERR("process '%s': fatal error %u in thread %p; "
				"halting system",
				proc->name, reason, (void *)cur);
			k_fatal_halt(reason);
			CODE_UNREACHABLE;
		}

		LOG_ERR("process '%s': fatal error %u in thread %p; "
			"aborting thread",
			proc->name, reason, (void *)cur);
		return;
	}

	LOG_PANIC();
	LOG_ERR("Halting system");
	k_fatal_halt(reason);
	CODE_UNREACHABLE;
}

#endif /* CONFIG_PROCESS_FATAL_HANDLER */
