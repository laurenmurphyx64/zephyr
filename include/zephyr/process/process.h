/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_PROCESS_PROCESS_H_
#define ZEPHYR_INCLUDE_PROCESS_PROCESS_H_

/**
 * @file
 * @brief Zephyr Process Model
 *
 * The process model groups one or more Zephyr threads into a single logical
 * unit ("process") with a shared lifecycle and (optionally) a shared
 * memory domain.
 *
 * A process may be either:
 *
 *  - **Native** — its threads run entry functions that are statically linked
 *    into the main Zephyr image.  No ELF loading and no LLEXT is involved.
 *    When @kconfig{CONFIG_USERSPACE} is enabled, a dedicated
 *    @c k_mem_domain is still created so the process's threads can share
 *    their stacks (and any partitions added via
 *    @ref z_process_add_partition) with each other while remaining
 *    isolated from the rest of the system.
 *
 *  - **LLEXT-backed** — code is loaded at runtime from an ELF binary via
 *    the @ref llext API.  The extension's TEXT/DATA/RODATA/BSS regions are
 *    added to the process's memory domain in addition to any per-thread
 *    stack partitions, so the process is hardware-isolated from the rest
 *    of the system.
 *
 * The number of threads per process is not statically bounded — threads
 * are tracked in a linked list with per-slot heap allocations.  The size
 * of the slot heap is controlled by @kconfig{CONFIG_PROCESS_HEAP_SIZE}.
 *
 * @defgroup process_apis Process Model
 * @since 4.2
 * @version 0.3.0
 * @ingroup os_services
 * @{
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>

#ifdef CONFIG_PROCESS_LLEXT
#include <zephyr/llext/llext.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Default name of the entry-point symbol inside an LLEXT extension */
#define Z_PROCESS_ENTRY_SYM "process_main"

/**
 * @brief Process backend kind
 */
enum z_process_kind {
	/** Code is linked into the main image; no ELF loading. */
	Z_PROCESS_KIND_NATIVE = 0,
	/** Code is loaded from an ELF binary via LLEXT. */
	Z_PROCESS_KIND_LLEXT,
};

/**
 * @brief Process lifecycle states
 */
enum z_process_state {
	/** Descriptor is initialised but holds no resources */
	Z_PROCESS_STATE_UNLOADED = 0,
	/** Process is ready to run (threads may be attached and started) */
	Z_PROCESS_STATE_LOADED,
	/** At least one process thread is running */
	Z_PROCESS_STATE_RUNNING,
	/** All process threads have exited */
	Z_PROCESS_STATE_DEAD,
};

/**
 * @brief Configuration options for a process
 *
 * Initialise with @ref Z_PROCESS_OPTS_DEFAULT and then override individual
 * fields as needed.  These options apply to *all* threads of the process.
 */
struct z_process_opts {
	/**
	 * Default stack size for threads added via @ref z_process_spawn.
	 * 0 selects @kconfig{CONFIG_PROCESS_STACK_SIZE_DEFAULT}.
	 */
	size_t stack_size;

	/** Default scheduling priority for threads of this process. */
	int priority;

	/**
	 * Run the process threads in unprivileged (user) mode.
	 * Requires both @kconfig{CONFIG_USERSPACE} and a process memory
	 * domain.  For native processes the caller is responsible for
	 * ensuring the thread entry function only calls APIs permitted to
	 * user threads.
	 * Default: @c true when CONFIG_USERSPACE is enabled.
	 */
	bool user_mode;

	/**
	 * Publish each thread's stack as a memory partition in the process
	 * domain so the process's threads can read/write each other's
	 * stacks.  Only meaningful when @kconfig{CONFIG_USERSPACE} is on
	 * (without it all kernel threads share the same address space
	 * anyway).
	 * Default: @c true.
	 */
	bool share_stacks;

	/**
	 * If @c true and @kconfig{CONFIG_PROCESS_FATAL_HANDLER} is enabled,
	 * halt the whole system when any thread of this process triggers a
	 * fatal error.  Otherwise the offending thread is simply aborted.
	 * Default: @c false.
	 */
	bool halt_on_fault;

	/**
	 * Name of the entry-point symbol used by @ref z_process_spawn (and
	 * @ref z_process_add_thread_sym when its @c entry_sym argument is
	 * NULL).  NULL selects @ref Z_PROCESS_ENTRY_SYM.
	 */
	const char *entry_sym;

	/** Argument passed to the implicit thread created by spawn. */
	void *arg;
};

/** @brief Default initialiser for @ref z_process_opts */
#define Z_PROCESS_OPTS_DEFAULT                                                 \
	{                                                                      \
		.stack_size    = 0,                                            \
		.priority      = CONFIG_PROCESS_PRIORITY_DEFAULT,              \
		.user_mode     = IS_ENABLED(CONFIG_USERSPACE),                 \
		.share_stacks  = true,                                         \
		.halt_on_fault = false,                                        \
		.entry_sym     = Z_PROCESS_ENTRY_SYM,                          \
		.arg           = NULL,                                         \
	}

/** @cond INTERNAL_HIDDEN */

/*
 * Per-thread bookkeeping slot.  One instance is heap-allocated for every
 * call to z_process_add_thread() (and friends) and linked into
 * z_process::threads.
 */
struct z_process_thread {
	sys_snode_t node;
	struct k_thread thread;
	k_thread_stack_t *stack;
	size_t stack_size;
	k_thread_entry_t entry_fn;
	void *arg;
	int priority;
	bool started;
	bool joined;
#ifdef CONFIG_USERSPACE
	struct k_mem_partition stack_part;
	bool stack_part_added;
#endif
};

/** @endcond */

/**
 * @brief Process descriptor.  Treat as opaque; use the API functions.
 */
struct z_process {
	/** @cond INTERNAL_HIDDEN */
	char name[CONFIG_PROCESS_NAME_MAX_LEN + 1];
	enum z_process_kind kind;

#ifdef CONFIG_PROCESS_LLEXT
	struct llext *ext;
	bool bringup_done;
#endif

#ifdef CONFIG_USERSPACE
	struct k_mem_domain domain;
	bool has_domain;
#endif

	sys_slist_t threads;
	unsigned int thread_count;

	volatile enum z_process_state state;
	int exit_code;

	struct z_process_opts opts;

	sys_snode_t reg_node;
	/** @endcond */
};

/**
 * @brief Define a stack suitable for a process thread.
 */
#define Z_PROCESS_STACK_DEFINE(_name, _size) K_THREAD_STACK_DEFINE(_name, _size)

/* -------------------------------------------------------------------------
 * Construction: native or LLEXT-backed
 * -----------------------------------------------------------------------*/

/**
 * @brief Initialise a native process descriptor.
 *
 * When @kconfig{CONFIG_USERSPACE} is enabled, a fresh (empty)
 * @c k_mem_domain is created so subsequently-added threads can share
 * stacks (when @c opts.share_stacks is true) and arbitrary partitions
 * added via @ref z_process_add_partition.
 *
 * @param proc  Descriptor to initialise (zeroed by the call)
 * @param name  Human-readable name
 * @param opts  Options; NULL selects defaults
 *
 * @retval 0       Success; process is in @ref Z_PROCESS_STATE_LOADED
 * @retval -EINVAL Bad argument
 * @retval <0      Error from @c k_mem_domain_init
 */
int z_process_init(struct z_process *proc, const char *name,
		   const struct z_process_opts *opts);

#ifdef CONFIG_PROCESS_LLEXT
/**
 * @brief Load an LLEXT-backed process from an ELF image in memory.
 *
 * Sets up an LLEXT and (with @kconfig{CONFIG_USERSPACE}) a memory domain
 * containing the extension's regions.  Does not create any threads.
 */
int z_process_load_ext(struct z_process *proc, const char *name,
		       const void *elf_data, size_t elf_size,
		       const struct z_process_opts *opts);
#endif

/* -------------------------------------------------------------------------
 * Thread attachment
 * -----------------------------------------------------------------------*/

/**
 * @brief Attach a native-function thread to a process.
 *
 * The number of threads per process is bounded only by the size of the
 * process slot heap (@kconfig{CONFIG_PROCESS_HEAP_SIZE}).
 *
 * @param proc        Process in LOADED state
 * @param stack       Stack memory (e.g. via @ref Z_PROCESS_STACK_DEFINE)
 * @param stack_size  Size of the stack in bytes
 * @param entry       Function to run in the new thread (Zephyr thread entry
 *                    signature: @c void(void*,void*,void*))
 * @param arg         Opaque argument forwarded as @c p1
 * @param thread_name Optional thread name (NULL = process name)
 *
 * @retval 0        Success
 * @retval -EINVAL  Bad argument or wrong state
 * @retval -ENOMEM  Process slot heap exhausted; raise
 *                  @kconfig{CONFIG_PROCESS_HEAP_SIZE}
 */
int z_process_add_thread(struct z_process *proc,
			 k_thread_stack_t *stack, size_t stack_size,
			 k_thread_entry_t entry, void *arg,
			 const char *thread_name);

#ifdef CONFIG_PROCESS_LLEXT
/**
 * @brief Attach a thread whose entry is an exported LLEXT symbol.
 *
 * @param entry_sym  Symbol name (NULL = @c opts.entry_sym)
 */
int z_process_add_thread_sym(struct z_process *proc,
			     k_thread_stack_t *stack, size_t stack_size,
			     const char *entry_sym, void *arg,
			     const char *thread_name);
#endif

/**
 * @brief Add a memory partition to the process's domain.
 *
 * After this call, every thread of @p proc (current and future) can access
 * @p part with the access mode the caller set on it.  Useful to expose a
 * shared buffer between threads of a native or LLEXT process, or to grant
 * a process access to a kernel-resident region that the caller controls.
 *
 * Requires @kconfig{CONFIG_USERSPACE}; on builds without it this function
 * is a successful no-op.
 *
 * @param proc  Process in LOADED state
 * @param part  Caller-allocated partition (must remain valid for the
 *              lifetime of the process)
 *
 * @retval 0        Success (or USERSPACE disabled)
 * @retval -EINVAL  Bad argument or process has no domain
 * @retval <0       Error from @c k_mem_domain_add_partition
 */
int z_process_add_partition(struct z_process *proc,
			    struct k_mem_partition *part);

/* -------------------------------------------------------------------------
 * Execution control
 * -----------------------------------------------------------------------*/

/**
 * @brief Start every attached, not-yet-started thread of the process.
 *
 * For LLEXT-backed processes the extension's .init_array runs in supervisor
 * mode on the first call.
 *
 * @retval 0        Process is now @ref Z_PROCESS_STATE_RUNNING
 * @retval -EINVAL  No threads attached or wrong state
 * @retval <0       LLEXT bringup error
 */
int z_process_start(struct z_process *proc);

#ifdef CONFIG_PROCESS_LLEXT
/**
 * @brief One-shot helper: load an LLEXT, attach one thread, start.
 */
int z_process_spawn(struct z_process *proc, const char *name,
		    const void *elf_data, size_t elf_size,
		    k_thread_stack_t *stack, size_t stack_size,
		    const struct z_process_opts *opts);

/**
 * @brief Legacy wrapper: load LLEXT + attach a single thread, but do not
 *        start the process.
 */
int z_process_load(struct z_process *proc, const char *name,
		   const void *elf_data, size_t elf_size,
		   k_thread_stack_t *stack, size_t stack_size,
		   const struct z_process_opts *opts);
#endif /* CONFIG_PROCESS_LLEXT */

/**
 * @brief Wait for all process threads to finish.
 *
 * @param timeout  Maximum time to wait per thread.  If it expires on any
 *                 thread, @c -EAGAIN is returned immediately.
 */
int z_process_join(struct z_process *proc, k_timeout_t timeout);

/**
 * @brief Abort every running thread of the process.
 */
int z_process_kill(struct z_process *proc);

/**
 * @brief Release all resources and reset the descriptor.
 */
void z_process_unload(struct z_process *proc);

/* -------------------------------------------------------------------------
 * Introspection
 * -----------------------------------------------------------------------*/

static inline enum z_process_state z_process_get_state(const struct z_process *proc)
{
	return proc->state;
}

static inline int z_process_exit_code(const struct z_process *proc)
{
	return proc->exit_code;
}

static inline unsigned int z_process_thread_count(const struct z_process *proc)
{
	return proc->thread_count;
}

/**
 * @brief Get the @c k_thread for the @p idx-th attached thread.
 *
 * Threads are tracked in attachment order.  Returns NULL if @p idx is out
 * of range (i.e. @p idx >= z_process_thread_count(proc)).  Because the
 * underlying slot is heap-allocated, the returned pointer is only stable
 * until the process is unloaded.
 */
struct k_thread *z_process_thread_get(struct z_process *proc,
				      unsigned int idx);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_PROCESS_PROCESS_H_ */
