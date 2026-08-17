.. zephyr:code-sample:: applet-hello-world
   :name: Applet "Hello World" sample
   :relevant-api: applet_apis

   Show the lifecycle of an applet that is either
   native or backed by LLEXT.

Overview
********

The sample demonstrates the use of the :ref:`applet` subsystem,
which allows for the grouping of threads into subapplications ("applets")
and simplified memory domain management for those applets. Users can
choose between a native applet, which is compiled into the Zephyr image,
or a LLEXT-backed applet, which is compiled as an ELF and loaded at runtime.

Specifically, this shows the lifecycle of a simple "hello world" applet,
implemented in
:zephyr_file:`samples/subsys/applet/hello_world/src/hello_world_applet.c`.
Depending on whether :kconfig:option:`CONFIG_LLEXT` is enabled,