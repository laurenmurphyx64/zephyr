.. zephyr:code-sample:: applet-shell-loader
   :name: Applet loader shell module

   Load and run LLEXT-backed applets using shell commands.

Overview
********

This example provides shell access to the applet subsystem, which is a wrapper
around :ref:`llext` (and native Zephyr threads) that automatically manages
threads, memory domains and privilege levels for a loaded extension.

It is the applet equivalent of the
:zephyr:code-sample:`llext-shell-loader` sample: an ELF is pasted into the
shell as a hex string, but instead of loading it and calling an exported
function directly, the extension is wrapped in an *applet*. The applet
subsystem then creates a thread for it, adds the extension's regions to a
dedicated memory domain, and runs the ``applet_main()`` entry point on that
thread.

Requirements
************

A board with a supported LLEXT architecture and a shell capable console. The
example below uses an ARMv7 target for illustration; the workflow is the same
on any LLEXT-supported architecture, only the toolchain prefix and the
generated hex payload change.

Building
********

The following command will build the main shell application:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/applet/shell_loader
   :board: robokit1
   :goals: build
   :compact:

.. note::

   You may need to disable memory protection for the sample to work (e.g.
   ``CONFIG_ARM_MPU=n`` on ARM, ``CONFIG_XTENSA_MMU=n`` /
   ``CONFIG_XTENSA_MPU=n`` on Xtensa, ``CONFIG_RISCV_PMP=n`` on RISC-V). See
   the full list of similar flags in
   :zephyr_file:`tests/subsys/llext/no_mem_protection.conf`.

This sample also includes the source for a very basic applet extension,
:zephyr_file:`samples/subsys/applet/shell_loader/hello_world.c`, which can be
used to test the applet features.

It can be compiled to :file:`build/hello_world.llext` using the Zephyr build
system like this:

.. code-block:: console

   $ ninja -C build -vvv hello_world_ext

On a host machine with the Zephyr SDK and the matching toolchain in ``PATH``,
the same object can also be produced directly. Pick the toolchain prefix that
matches your target, for example ``arm-zephyr-eabi-`` for ARM,
``xtensa-<soc>_zephyr-elf-`` for Xtensa or ``riscv64-zephyr-elf-`` for
RISC-V:

.. code-block:: console

   $ <toolchain-prefix>gcc -mlong-calls -c -o build/hello_world.llext samples/subsys/applet/shell_loader/hello_world.c

.. note::

   The extension must be rebuilt for each target architecture: an extension
   compiled for ARM cannot be loaded on Xtensa or RISC-V, and vice versa. The
   loader will accept the hex string but the CPU will trap when running the
   applet because the instruction stream is foreign to it.

.. note::

   The applet subsystem looks up the entry point by symbol name
   (``applet_main`` by default, see ``APPLET_ENTRY_SYM``), so that symbol must
   be present in the extension's export table. LLEXT by default only exports
   symbols explicitly marked with the :c:macro:`EXPORT_SYMBOL` macro, which
   requires using the full Zephyr build system, or at least the
   :ref:`LLEXT EDK <llext_build_edk>`.

   To avoid this complexity, this sample configures Zephyr to use all global
   symbols defined in the extension ELF file via the Kconfig option
   :kconfig:option:`CONFIG_LLEXT_IMPORT_ALL_GLOBALS`. This is not recommended
   for large extensions as the memory usage increases significantly.

The compiled extension can be inspected with the usual binutils utilities and
then converted to a hex string usable by the ``applet load_hex`` shell command:

.. code-block:: console

  $ <toolchain-prefix>objdump -r -d -x build/hello_world.llext
  $ xxd -p -c 99999 build/hello_world.llext

Running
*******

Once the board has booted, you will be presented with a shell prompt.
All the applet related commands are available as sub-commands of ``applet``,
and can be seen with ``applet help``:

.. code-block:: console

  uart:~$ applet help
  applet - Applet commands
  Subcommands:
    list      :List loaded applets, their state and thread count
    load_hex  :Load an elf file encoded in hex directly from the shell input.
               Syntax:
               <applet_name> <ext_hex_string>
    start     :Start a loaded applet, running its applet_main() entry point.
               Syntax:
               <applet_name>
    join      :Wait for every thread of an applet to finish.
               Syntax:
               <applet_name> [timeout_ms]
    kill      :Abort every running thread of an applet.
               Syntax:
               <applet_name>
    unload    :Unload an applet and release its ELF buffer.
               Syntax:
               <applet_name>

The hex string generated above can be used to load the extension as an applet:

.. code-block:: console

  uart:~$ applet load_hex hello_world <hex>
  Successfully loaded applet hello_world (880 bytes)

Loading an applet does not run any of its code yet: it only loads the ELF and
attaches a thread to the ``applet_main`` entry point. The applet can then be
listed, started, waited on, and unloaded:

.. code-block:: console

  uart:~$ applet list
  | Name             | State      | Threads |
  |      hello_world |     loaded |       1 |
  uart:~$ applet start hello_world
  hello world from applet (arg=0)
  Started applet hello_world
  uart:~$ applet join hello_world
  Applet hello_world finished with exit code 0
  uart:~$ applet unload hello_world
  Unloaded applet hello_world

An applet that does not terminate on its own can be stopped with
``applet kill``, which aborts every thread of that applet without touching the
rest of the system.

Loading multiple applets
************************

Each ``load_hex`` invocation allocates its own ELF buffer and stack slot, and
tracks them under the applet name. Both are released when ``unload`` is called
for that name, so several applets can stay loaded and run independently:

.. code-block:: console

  uart:~$ applet load_hex hello_world <hex>
  uart:~$ applet load_hex worker <hex>
  uart:~$ applet list
  | Name             | State      | Threads |
  |      hello_world |     loaded |       1 |
  |           worker |     loaded |       1 |
  uart:~$ applet start hello_world
  uart:~$ applet start worker
  uart:~$ applet unload worker

The number of simultaneously loaded applets is limited by the
``APPLET_SHELL_MAX_LOADED`` slot count in
:zephyr_file:`samples/subsys/applet/shell_loader/src/main.c` (four by default).
Adjust it there if more concurrent applets are needed.

Running applets in user mode
****************************

When :kconfig:option:`CONFIG_USERSPACE` is enabled, the applet subsystem places
each applet in its own memory domain and runs its threads unprivileged, so a
misbehaving applet cannot corrupt the kernel or other applets. This requires a
target with hardware memory protection, which is disabled in this sample's
default configuration to keep the hex loading workflow simple.
