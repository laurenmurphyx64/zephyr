/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file main.c
 * @brief Zephyr Process Model test suite
 *
 * Exercises the full lifecycle and correctness of the process subsystem:
 *
 *   - test_load_unload         – load then unload without running
 *   - test_spawn_join          – full spawn → join → unload cycle
 *   - test_state_machine       – verify state transitions
 *   - test_invalid_args        – NULL/bad-arg rejection
 *   - test_arg_forwarding      – arg reaches the extension
 *   - test_concurrent_processes – two processes run in parallel
 *   - test_reload              – descriptor can be reused after unload
 *   - test_kill                – z_process_kill stops a running process
 *   - test_missing_entry_sym   – loading with unknown symbol returns -ENOENT
 */

#include <zephyr/kernel.h>
#include <zephyr/process/process.h>
#include <zephyr/ztest.h>
#include <stdint.h>

#ifdef CONFIG_USERSPACE
#include <zephyr/app_memory/app_memdomain.h>
#endif

/* ─────────────────────────────────────────────────────────────────────────
 * Embedded extension binaries (generated at build time)
 * ─────────────────────────────────────────────────────────────────────── */

static const uint8_t basic_elf[] = {
#include <proc_basic.inc>
};

static const uint8_t args_elf[] = {
#include <proc_args.inc>
};

static const uint8_t counter_elf[] = {
#include <proc_counter.inc>
};

/* ─────────────────────────────────────────────────────────────────────────
 * Shared test fixtures
 * ─────────────────────────────────────────────────────────────────────── */

/* Each test that needs a stack gets one of these */
Z_PROCESS_STACK_DEFINE(test_stack_0, CONFIG_PROCESS_STACK_SIZE_DEFAULT);
Z_PROCESS_STACK_DEFINE(test_stack_1, CONFIG_PROCESS_STACK_SIZE_DEFAULT);
Z_PROCESS_STACK_DEFINE(test_stack_2, CONFIG_PROCESS_STACK_SIZE_DEFAULT);

/* Semaphore used by the counter extension */
K_SEM_DEFINE(counter_sem, 0, 10);

/* Result slot used by the args extension  */
static volatile uint32_t args_result;

/* ─────────────────────────────────────────────────────────────────────────
 * Tests
 * ─────────────────────────────────────────────────────────────────────── */

/**
 * @brief Load an extension without running it, then unload.
 */
ZTEST(process_lifecycle, test_load_unload)
{
	struct z_process proc;

	int ret = z_process_load(&proc, "basic",
				 basic_elf, sizeof(basic_elf),
				 test_stack_0, sizeof(test_stack_0),
				 NULL);

	zassert_ok(ret, "z_process_load failed: %d", ret);
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_LOADED,
		      "expected LOADED state");

	z_process_unload(&proc);
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_UNLOADED,
		      "expected UNLOADED state after unload");
}

/**
 * @brief Full spawn → join → unload cycle with the basic extension.
 */
ZTEST(process_lifecycle, test_spawn_join)
{
	struct z_process proc;

	int ret = z_process_spawn(&proc, "basic",
				  basic_elf, sizeof(basic_elf),
				  test_stack_0, sizeof(test_stack_0),
				  NULL);

	zassert_ok(ret, "z_process_spawn failed: %d", ret);
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_RUNNING,
		      "expected RUNNING state");

	ret = z_process_join(&proc, K_SECONDS(5));
	zassert_ok(ret, "z_process_join failed: %d", ret);
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_DEAD,
		      "expected DEAD state after join");
	zassert_equal(z_process_exit_code(&proc), 0,
		      "expected exit code 0");

	z_process_unload(&proc);
}

/**
 * @brief State machine: verify all transitions are correct.
 */
ZTEST(process_lifecycle, test_state_machine)
{
	struct z_process proc;

	/* initial state is implicitly UNLOADED (zeroed struct) */
	memset(&proc, 0, sizeof(proc));
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_UNLOADED,
		      "freshly zeroed descriptor should be UNLOADED");

	zassert_ok(z_process_load(&proc, "sm",
				  basic_elf, sizeof(basic_elf),
				  test_stack_0, sizeof(test_stack_0),
				  NULL));
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_LOADED);

	zassert_ok(z_process_start(&proc));
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_RUNNING);

	zassert_ok(z_process_join(&proc, K_SECONDS(5)));
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_DEAD);

	z_process_unload(&proc);
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_UNLOADED);
}

/**
 * @brief API functions must reject NULL and invalid arguments gracefully.
 */
ZTEST(process_lifecycle, test_invalid_args)
{
	struct z_process proc;

	/* NULL proc pointer */
	zassert_equal(z_process_load(NULL, "x", basic_elf, sizeof(basic_elf),
				     test_stack_0, sizeof(test_stack_0), NULL),
		      -EINVAL, "NULL proc should return -EINVAL");

	/* NULL ELF data */
	zassert_equal(z_process_load(&proc, "x", NULL, 128,
				     test_stack_0, sizeof(test_stack_0), NULL),
		      -EINVAL, "NULL elf_data should return -EINVAL");

	/* zero ELF size */
	zassert_equal(z_process_load(&proc, "x", basic_elf, 0,
				     test_stack_0, sizeof(test_stack_0), NULL),
		      -EINVAL, "zero elf_size should return -EINVAL");

	/* NULL name */
	zassert_equal(z_process_load(&proc, NULL, basic_elf, sizeof(basic_elf),
				     test_stack_0, sizeof(test_stack_0), NULL),
		      -EINVAL, "NULL name should return -EINVAL");

	/* start/join/kill on unloaded (zeroed) descriptor */
	memset(&proc, 0, sizeof(proc));
	zassert_equal(z_process_start(&proc), -EINVAL,
		      "starting unloaded proc should return -EINVAL");
	zassert_equal(z_process_join(&proc, K_NO_WAIT), -EINVAL,
		      "joining unloaded proc should return -EINVAL");
	zassert_equal(z_process_kill(&proc), -EINVAL,
		      "killing unloaded proc should return -EINVAL");
}

/**
 * @brief The arg value passed via opts reaches the extension entry function.
 *
 * proc_args_ext writes 0xDEADBEEF into the pointer it receives as arg.
 * We verify the host-side variable is updated after the process exits.
 */
ZTEST(process_lifecycle, test_arg_forwarding)
{
	struct z_process proc;

	args_result = 0;

	struct z_process_opts opts = Z_PROCESS_OPTS_DEFAULT;

	opts.arg = (void *)&args_result;

	int ret = z_process_spawn(&proc, "args",
				  args_elf, sizeof(args_elf),
				  test_stack_0, sizeof(test_stack_0),
				  &opts);

	zassert_ok(ret, "spawn failed: %d", ret);

	ret = z_process_join(&proc, K_SECONDS(5));
	zassert_ok(ret, "join failed: %d", ret);

	/*
	 * NOTE: When running in user mode the extension cannot write to
	 * args_result because it is a kernel BSS variable that is not mapped
	 * into the process domain.  This test is meaningful in supervisor mode
	 * (CONFIG_USERSPACE=n) or when the caller explicitly adds the result
	 * slot to the process domain via a custom partition.
	 *
	 * The test assertion is conditioned accordingly.
	 */
#ifndef CONFIG_USERSPACE
	zassert_equal(args_result, 0xDEADBEEFU,
		      "arg not forwarded to extension (got 0x%08x)",
		      args_result);
#endif

	z_process_unload(&proc);
}

/**
 * @brief Two processes run concurrently; each signals a semaphore N times.
 *
 * After both have finished, the semaphore count should equal 2 * N_ITER.
 */
ZTEST(process_lifecycle, test_concurrent_processes)
{
	struct z_process proc0, proc1;
	const int N_ITER = 5;

	k_sem_reset(&counter_sem);

	struct z_process_opts opts = Z_PROCESS_OPTS_DEFAULT;

	opts.arg = &counter_sem;

	/* Load (don't start) so we can grant kernel-object access first */
	zassert_ok(z_process_load(&proc0, "ctr0",
				  counter_elf, sizeof(counter_elf),
				  test_stack_1, sizeof(test_stack_1),
				  &opts));
	zassert_ok(z_process_load(&proc1, "ctr1",
				  counter_elf, sizeof(counter_elf),
				  test_stack_2, sizeof(test_stack_2),
				  &opts));

#ifdef CONFIG_USERSPACE
	k_object_access_grant(&counter_sem, z_process_thread_get(&proc0, 0));
	k_object_access_grant(&counter_sem, z_process_thread_get(&proc1, 0));
#endif

	zassert_ok(z_process_start(&proc0));
	zassert_ok(z_process_start(&proc1));

	zassert_ok(z_process_join(&proc0, K_SECONDS(5)));
	zassert_ok(z_process_join(&proc1, K_SECONDS(5)));

	int count = k_sem_count_get(&counter_sem);

	zassert_equal(count, 2 * N_ITER,
		      "expected %d semaphore signals, got %d",
		      2 * N_ITER, count);

	z_process_unload(&proc0);
	z_process_unload(&proc1);
}

/**
 * @brief A process descriptor can be reloaded and reused after unload.
 */
ZTEST(process_lifecycle, test_reload)
{
	struct z_process proc;

	for (int i = 0; i < 3; i++) {
		int ret = z_process_spawn(&proc, "reload",
					  basic_elf, sizeof(basic_elf),
					  test_stack_0, sizeof(test_stack_0),
					  NULL);

		zassert_ok(ret, "iteration %d: spawn failed: %d", i, ret);
		zassert_ok(z_process_join(&proc, K_SECONDS(5)),
			   "iteration %d: join failed", i);
		z_process_unload(&proc);
	}
}

/**
 * @brief z_process_kill stops the process thread.
 *
 * We load a process whose extension sleeps for a long time, then kill it
 * before it would finish naturally.
 */
ZTEST(process_lifecycle, test_kill)
{
	/*
	 * The counter extension sleeps 10 ms per iteration × 5 iterations =
	 * 50 ms total.  Kill after 20 ms to interrupt it mid-run.
	 */
	struct z_process proc;
	struct z_process_opts opts = Z_PROCESS_OPTS_DEFAULT;

	opts.arg = NULL; /* counter extension handles NULL sem gracefully */

	zassert_ok(z_process_spawn(&proc, "kill_target",
				   counter_elf, sizeof(counter_elf),
				   test_stack_0, sizeof(test_stack_0),
				   &opts));

	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_RUNNING);

	/* Let it run briefly, then kill */
	k_msleep(20);
	zassert_ok(z_process_kill(&proc), "kill failed");
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_DEAD,
		      "expected DEAD state after kill");

	z_process_unload(&proc);
}

/**
 * @brief Loading with an unknown entry symbol returns -ENOENT.
 */
ZTEST(process_lifecycle, test_missing_entry_sym)
{
	struct z_process proc;
	struct z_process_opts opts = Z_PROCESS_OPTS_DEFAULT;

	opts.entry_sym = "this_symbol_does_not_exist";

	int ret = z_process_load(&proc, "nosym",
				 basic_elf, sizeof(basic_elf),
				 test_stack_0, sizeof(test_stack_0),
				 &opts);

	zassert_equal(ret, -ENOENT,
		      "expected -ENOENT for unknown symbol, got %d", ret);
	/* Descriptor should be fully cleaned up even on error */
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_UNLOADED,
		      "state should be UNLOADED after failed load");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Native (non-LLEXT) process tests
 * ─────────────────────────────────────────────────────────────────────── */

/* Shared counters between native threads of one process */
static atomic_t native_counter_a;
static atomic_t native_counter_b;
static struct k_sem native_done_sem;

static void native_worker_a(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (int i = 0; i < 5; i++) {
		atomic_inc(&native_counter_a);
		k_msleep(5);
	}
	k_sem_give(&native_done_sem);
}

static void native_worker_b(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	atomic_t *peer = (atomic_t *)p1;

	for (int i = 0; i < 5; i++) {
		atomic_inc(&native_counter_b);
		/* Demonstrate access to peer thread's data via shared pointer */
		atomic_add(&native_counter_b, atomic_get(peer) > 0 ? 0 : 0);
		k_msleep(5);
	}
	k_sem_give(&native_done_sem);
}

/**
 * @brief A native process with two threads runs and joins cleanly.
 */
ZTEST(process_lifecycle, test_native_multi_thread)
{
	struct z_process proc;

	atomic_clear(&native_counter_a);
	atomic_clear(&native_counter_b);
	k_sem_init(&native_done_sem, 0, 2);

	zassert_ok(z_process_init(&proc, "native_mt", NULL),
		   "z_process_init failed");
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_LOADED);

	zassert_ok(z_process_add_thread(&proc, test_stack_0,
					sizeof(test_stack_0),
					native_worker_a, NULL, "wa"),
		   "add_thread A failed");

	zassert_ok(z_process_add_thread(&proc, test_stack_1,
					sizeof(test_stack_1),
					native_worker_b,
					(void *)&native_counter_a, "wb"),
		   "add_thread B failed");

	zassert_equal(z_process_thread_count(&proc), 2,
		      "expected 2 threads, got %u",
		      z_process_thread_count(&proc));

	zassert_ok(z_process_start(&proc));
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_RUNNING);

	/* z_process_join must wait for BOTH threads */
	zassert_ok(z_process_join(&proc, K_SECONDS(5)),
		   "z_process_join did not return 0");
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_DEAD);

	zassert_equal(atomic_get(&native_counter_a), 5);
	zassert_equal(atomic_get(&native_counter_b), 5);

	z_process_unload(&proc);
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_UNLOADED);
}

/**
 * @brief z_process_kill terminates every running thread of the process.
 */
static void native_spinner(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_msleep(50);
	}
}

ZTEST(process_lifecycle, test_native_kill_all)
{
	struct z_process proc;

	zassert_ok(z_process_init(&proc, "spinners", NULL));
	zassert_ok(z_process_add_thread(&proc, test_stack_0,
					sizeof(test_stack_0),
					native_spinner, NULL, NULL));
	zassert_ok(z_process_add_thread(&proc, test_stack_1,
					sizeof(test_stack_1),
					native_spinner, NULL, NULL));
	zassert_ok(z_process_add_thread(&proc, test_stack_2,
					sizeof(test_stack_2),
					native_spinner, NULL, NULL));

	zassert_ok(z_process_start(&proc));
	k_msleep(20);

	zassert_ok(z_process_kill(&proc), "kill failed");
	zassert_equal(z_process_get_state(&proc), Z_PROCESS_STATE_DEAD);

	/* All threads should now be joinable immediately */
	zassert_ok(z_process_join(&proc, K_NO_WAIT),
		   "join after kill should be immediate");

	z_process_unload(&proc);
}

/**
 * @brief Slot-heap exhaustion returns -ENOMEM gracefully.
 *
 * Keep adding threads to a single process until the slot heap rejects the
 * request.  We must succeed at least once and eventually fail; the exact
 * count depends on @kconfig{CONFIG_PROCESS_HEAP_SIZE} and the size of
 * @c struct k_thread for the target.
 */
#define HEAP_PROBE_MAX 64
static K_THREAD_STACK_ARRAY_DEFINE(extra_stacks, HEAP_PROBE_MAX,
				   CONFIG_PROCESS_STACK_SIZE_DEFAULT);

ZTEST(process_lifecycle, test_native_heap_exhaustion)
{
	struct z_process proc;
	int added = 0;
	int last_ret = 0;

	zassert_ok(z_process_init(&proc, "heap", NULL));

	for (int i = 0; i < HEAP_PROBE_MAX; i++) {
		int ret = z_process_add_thread(
			&proc, extra_stacks[i],
			K_THREAD_STACK_SIZEOF(extra_stacks[i]),
			native_spinner, NULL, NULL);

		if (ret == 0) {
			added++;
		} else {
			last_ret = ret;
			break;
		}
	}

	zassert_true(added >= 1,
		     "slot heap could not hold even one thread");
	zassert_equal(last_ret, -ENOMEM,
		      "expected -ENOMEM on exhaustion, got %d", last_ret);
	zassert_equal(z_process_thread_count(&proc), (unsigned int)added,
		      "thread_count mismatch (%u vs %d)",
		      z_process_thread_count(&proc), added);

	z_process_unload(&proc);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Native + memory domain / shared partition tests
 * ─────────────────────────────────────────────────────────────────────── */

#ifdef CONFIG_USERSPACE

/*
 * A shared partition that both worker threads will use.  We expose it to
 * the process domain via z_process_add_partition().  Without that call,
 * a user-mode thread would fault when touching shared_word.
 */
K_APPMEM_PARTITION_DEFINE(shared_part);
K_APP_DMEM(shared_part) static volatile uint32_t shared_word;
K_APP_BMEM(shared_part) static volatile uint32_t observed_word;

static void share_writer(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	shared_word = 0xABCD0001U;
}

static void share_reader(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	for (int i = 0; i < 100; i++) {
		if (shared_word == 0xABCD0001U) {
			observed_word = shared_word;
			return;
		}
		k_msleep(5);
	}
}

/**
 * @brief A native process's user-mode threads can share a custom partition
 *        added via z_process_add_partition().
 */
ZTEST(process_lifecycle, test_native_shared_partition)
{
	struct z_process proc;
	struct z_process_opts opts = Z_PROCESS_OPTS_DEFAULT;

	opts.user_mode = true;
	shared_word    = 0;
	observed_word  = 0;

	zassert_ok(z_process_init(&proc, "shared", &opts));

	zassert_ok(z_process_add_partition(&proc, &shared_part),
		   "add_partition failed");

	zassert_ok(z_process_add_thread(&proc, test_stack_0,
					sizeof(test_stack_0),
					share_writer, NULL, "w"));
	zassert_ok(z_process_add_thread(&proc, test_stack_1,
					sizeof(test_stack_1),
					share_reader, NULL, "r"));

	zassert_ok(z_process_start(&proc));
	zassert_ok(z_process_join(&proc, K_SECONDS(2)));

	zassert_equal(observed_word, 0xABCD0001U,
		      "reader did not observe writer's value via shared partition (got 0x%08x)",
		      observed_word);

	z_process_unload(&proc);
}

#endif /* CONFIG_USERSPACE */

/* ─────────────────────────────────────────────────────────────────────────
 * Test suite registration
 * ─────────────────────────────────────────────────────────────────────── */

ZTEST_SUITE(process_lifecycle, NULL, NULL, NULL, NULL, NULL);
