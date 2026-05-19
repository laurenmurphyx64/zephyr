.. _process_model_overview:

Overview
########

The Process Model subsystem groups one or more Zephyr threads into a single
logical "process" with a shared lifecycle and (optionally) a shared memory
domain.  A process can be either:

.. list-table::
   :widths: 25 75
   :header-rows: 1

   * - Process kind
     - Description
   * - **Native**
     - The process's thread entry functions are statically linked into the
       main Zephyr image.  No ELF loading and no LLEXT is involved.  When
       :kconfig:option:`CONFIG_USERSPACE` is enabled a dedicated
       :c:struct:`k_mem_domain` is still created so the process's threads
       can share their stacks (and any partitions added via
       :c:func:`z_process_add_partition`) with each other while remaining
       isolated from the rest of the system.  Available whether or not
       :ref:`llext` is enabled.
   * - **LLEXT-backed**
     - The process's code is loaded at runtime from an ELF binary via
       :ref:`llext`.  The same dedicated :c:struct:`k_mem_domain` also
       contains the extension's TEXT/DATA/RODATA/BSS regions as
       :c:struct:`k_mem_partition` objects, giving the process
       hardware-enforced isolation.  When
       :kconfig:option:`CONFIG_USERSPACE` is enabled the threads run in
       unprivileged mode by default.  Requires
       :kconfig:option:`CONFIG_PROCESS_LLEXT` (auto-selected when
       :kconfig:option:`CONFIG_LLEXT` is enabled).

A process is described by a :c:struct:`z_process` descriptor that the caller
allocates (global, static, or heap) and keeps valid for the process's
lifetime.  The number of threads attached to a process is not statically
bounded — each :c:func:`z_process_add_thread` (and friend) allocates a
bookkeeping slot from a private :c:struct:`k_heap` whose size is set by
:kconfig:option:`CONFIG_PROCESS_HEAP_SIZE`.  If that heap is exhausted,
attach calls return ``-ENOMEM``.

Quick comparison
****************

Native process with multiple threads::

   Z_PROCESS_STACK_DEFINE(s_a, 1024);
   Z_PROCESS_STACK_DEFINE(s_b, 1024);

   struct z_process p;

   z_process_init(&p, "workers", NULL);
   z_process_add_thread(&p, s_a, sizeof(s_a), worker_a, NULL, NULL);
   z_process_add_thread(&p, s_b, sizeof(s_b), worker_b, NULL, NULL);
   z_process_start(&p);
   z_process_join(&p, K_FOREVER);   /* waits for BOTH threads */
   z_process_unload(&p);

LLEXT-backed single-thread process (the original one-shot convenience API)::

   z_process_spawn(&p, "myext", elf, sz, stack, stack_sz, NULL);
   z_process_join(&p, K_FOREVER);
   z_process_unload(&p);

Concepts
********

Process kind
============

The kind is selected at construction time and cannot be changed afterwards:

- :c:func:`z_process_init` creates a **native** process.
- :c:func:`z_process_load_ext` (and the convenience helpers
  :c:func:`z_process_load` / :c:func:`z_process_spawn`) creates an
  **LLEXT-backed** process.

LLEXT-only API functions are conditionally compiled under
:kconfig:option:`CONFIG_PROCESS_LLEXT`.

Threads
=======

Threads are attached to an existing process descriptor via:

- :c:func:`z_process_add_thread` – takes a native ``k_thread_entry_t``.
  Use this for native processes (and optionally for LLEXT processes when
  the supervisor wants to inject a helper thread).
- :c:func:`z_process_add_thread_sym` – looks up the entry by symbol name in
  the loaded extension's export table.  LLEXT-backed processes only.

Threads are created in a suspended state when attached, so the caller may
obtain the resulting :c:struct:`k_thread` pointer via
:c:func:`z_process_thread_get` and pass it to APIs like
:c:func:`k_object_access_grant` *before* the process is started.  Note that
thread slots are heap-allocated on attach — the :c:struct:`k_thread`
pointer is only valid **after** the corresponding
:c:func:`z_process_add_thread` call has returned.

Lifecycle
=========

.. code-block:: none

   UNLOADED ──init() / load_ext()──► LOADED ──add_thread()──► LOADED
                                       │
                                       └──start()──► RUNNING
                                                        │
                                       ◄──unload()── DEAD
                                                        ▲
                                              join()  / kill()

.. list-table::
   :widths: 20 80
   :header-rows: 1

   * - State
     - Meaning
   * - ``UNLOADED``
     - Descriptor is zeroed or has been reset by :c:func:`z_process_unload`.
       No resources are held.
   * - ``LOADED``
     - Descriptor is initialised.  Threads may be attached.  For LLEXT
       processes, the extension is loaded and the domain is set up; for
       native processes, nothing else is held.
   * - ``RUNNING``
     - All attached threads have been released to the scheduler.
       (Some may have already finished.)
   * - ``DEAD``
     - Every attached thread has exited or been aborted.

Joining and killing
===================

:c:func:`z_process_join` waits for **all** attached threads to finish.  The
timeout applies to each thread individually; if it expires on any thread,
the function returns ``-EAGAIN`` immediately without joining the rest.

:c:func:`z_process_kill` aborts every running thread of the process and
moves the process to ``DEAD``.

Stack sharing and custom partitions
===================================

Whenever :kconfig:option:`CONFIG_USERSPACE` is enabled the process — native
or LLEXT-backed — owns a dedicated :c:struct:`k_mem_domain`.  By default
(``opts.share_stacks = true``) every attached thread's stack is added to
that domain as a :c:struct:`k_mem_partition` with read/write access for
both supervisor and user mode, so peer threads of the same process can
read and write each other's stacks.  Set ``opts.share_stacks = false`` to
keep stacks private to their owning thread.

Callers can publish additional partitions to the process domain at any
time after construction and before/after :c:func:`z_process_start` via::

   int z_process_add_partition(struct z_process *proc,
                               struct k_mem_partition *part);

This is the supported way to share an arbitrary memory region (for IPC,
shared state, DMA buffers, etc.) between all of a process's threads,
whether the process is native or LLEXT-backed.  Without
:kconfig:option:`CONFIG_USERSPACE`, the function is a successful no-op
since all kernel threads already share the same address space.

When :kconfig:option:`CONFIG_USERSPACE` is disabled, ``share_stacks`` and
``z_process_add_partition`` have no effect: every kernel thread can
already access every other thread's memory.

Memory layout (LLEXT-backed, userspace)
=======================================

.. code-block:: none

   ┌─────────────────────────────────────────────────────┐
   │  LLEXT heap                                         │
   │   ┌─────────────────────────────────────────────┐   │
   │   │ Extension TEXT   (P_RX_U_RX)                │   │
   │   │ Extension DATA   (P_RW_U_RW)                │   │
   │   │ Extension RODATA (P_RO_U_RO)                │   │
   │   │ Extension BSS    (P_RW_U_RW)                │   │
   │   └─────────────────────────────────────────────┘   │
   └─────────────────────────────────────────────────────┘
   ┌─────────────────────────────────────────────────────┐
   │  Thread stacks (one per attached thread)            │
   │   ─ Added as P_RW_U_RW partitions when              │
   │     opts.share_stacks is true (the default)         │
   └─────────────────────────────────────────────────────┘

When running in user mode each process thread can access only:

1. Its own stack
2. The peer thread stacks (when ``share_stacks`` is enabled)
3. The four LLEXT partitions

All other memory — the :c:struct:`z_process` descriptor itself, kernel data
structures, and other processes' regions — is inaccessible.  Attempting to
access it triggers a memory protection fault.

User Mode Execution
===================

When :kconfig:option:`CONFIG_USERSPACE` is enabled and ``opts.user_mode``
is true, the supervisor trampoline calls
:c:func:`k_thread_user_mode_enter` to drop privileges before invoking the
thread's entry function.

Native processes default to **supervisor** mode when constructed with
``opts == NULL`` (the most common case for native processes, whose entry
functions usually call APIs not available to user threads).  Callers who
want native user-mode threads must explicitly set ``opts.user_mode =
true`` and pass the populated options struct.

LLEXT-backed processes default to **user** mode when
:kconfig:option:`CONFIG_USERSPACE` is enabled.  For these processes:

- ``.init_array`` always runs in supervisor mode (once per process, on the
  first :c:func:`z_process_start`).
- Application code in each thread runs in user mode.
- ``.fini_array`` is called by :c:func:`z_process_unload` in supervisor
  mode after the threads have exited.

Fatal-error policy
==================

When :kconfig:option:`CONFIG_PROCESS_FATAL_HANDLER` is enabled, the
subsystem installs a custom :c:func:`k_sys_fatal_error_handler` that
behaves as follows:

- If the faulting thread belongs to a process whose
  :c:member:`z_process_opts.halt_on_fault` is ``true``, the whole system
  is halted via :c:func:`k_fatal_halt`.
- Otherwise the offending thread is simply aborted (the default Zephyr
  behaviour).

This option is mutually exclusive with an application-provided
:c:func:`k_sys_fatal_error_handler`.

Inter-Process Communication
===========================

Two separate processes cannot share writable memory directly; each has its
own :c:struct:`k_mem_domain` and cannot see the other's partitions.

IPC must go through kernel primitives (message queues, pipes, etc.) or via
an explicitly-shared :c:struct:`k_mem_partition` that the host supervisor
publishes to **both** processes with :c:func:`z_process_add_partition`.

For kernel-object IPC (message queues, semaphores, …) user-mode threads
need an explicit :c:func:`k_object_access_grant`.  Because thread
bookkeeping slots are heap-allocated on attach, the :c:struct:`k_thread`
pointer is only valid **after** the thread has been added; grant access
between :c:func:`z_process_add_thread` (or :c:func:`z_process_load`) and
:c:func:`z_process_start`::

   z_process_load(&proc, "worker", elf, sz, stack, stack_sz, &opts);
   /* Now z_process_thread_get(&proc, 0) returns a valid k_thread *. */
   k_object_access_grant(&my_msgq, z_process_thread_get(&proc, 0));
   z_process_start(&proc);
