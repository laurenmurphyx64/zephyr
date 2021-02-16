.. tensorflow_hello_world:

TensorFlow Hello World
######################

Overview
********

A simple TensorFlow sample that replicates a sine wave.

Building and Running
********************

This application can be built and executed on QEMU as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/tensorflow/hello_world
   :host-os: unix
   :board: qemu_x86
   :goals: run
   :compact:

Exit QEMU by pressing :kbd:`CTRL+A` :kbd:`x`.

This application can be built for LiteX VexRiscv as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/tensorflow/hello_world
   :host-os: unix
   :board: litex_vexriscv
   :goals: build
   :compact:

The resulting binary can be executed on Renode as follows:

.. code-block:: console

    renode -e "set zephyr_elf @./build/zephyr/zephyr.elf; s @../renode/litex-vexriscv-tflite.resc"

Sample Output
=============

.. code-block:: console

    ...

    x_value: 1.0995567*2^1, y_value: 1.6951603*2^-1

    x_value: 1.2566366*2^1, y_value: 1.1527088*2^-1

    x_value: 1.4137159*2^1, y_value: 1.1527088*2^-2

    x_value: 1.5707957*2^1, y_value: -1.0849024*2^-6

    x_value: 1.7278753*2^1, y_value: -1.0509993*2^-2

    ...
