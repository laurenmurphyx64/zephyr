.. _tensorflow_magic_wand:

TensorFlow Magic Wand
#####################

Overview
********

This sample application shows how to use TensorFlow Lite Micro
to run a 20 kilobyte neural network model that recognizes gestures
from an accelerometer.

.. Note::
    This README and sample have been modified from
    `the TensorFlow Magic Wand sample for Zephyr`_ and
    `the Antmicro tutorial on Renode emulation for TensorFlow`_.

.. _the TensorFlow Magic Wand sample for Zephyr:
    https://github.com/tensorflow/tensorflow/tree/master/tensorflow/lite/micro/examples/magic_wand

.. _the Antmicro tutorial on Renode emulation for TensorFlow:
    https://github.com/antmicro/litex-vexriscv-tensorflow-lite-demo

Building and Running
********************

This sample should work on boards that have either the FXOS8700 or
the ADXL345 accelerometer.

This application can be built and executed on the :ref:`frdm_k64f`
as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/tensorflow/magic_wand
   :host-os: unix
   :board: frdm_k64f
   :goals: build flash
   :compact:

The application can be built for the :ref:`litex-vexriscv` for
emulation in Renode as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/tensorflow/magic_wand
   :host-os: unix
   :board: litex_vexriscv
   :goals: build
   :compact:

Once the application is built, `download and install Renode`_
following the instructions in the `Renode GitHub README`_ and
start the emulator:

.. code-block:: console

    renode -e "set zephyr_elf @./build/zephyr/zephyr.elf; s @./renode/litex-vexriscv-tflite.resc"

.. _download and install Renode:
    https://github.com/renode/renode/releases/

.. _Renode GitHub README:
    https://github.com/renode/renode/blob/master/README.rst

Sample Output
=============

Align the accelerometer at rest and then make one of the recognizable
gestures (wing, ring or slope) and you should observe one of the
following:

.. code-block:: console

    Got accelerometer, label: FXOS8700

    WING:
    *         *         *
     *       * *       *
      *     *   *     *
       *   *     *   *
        * *       * *
         *         *

    RING:
              *
           *     *
         *         *
        *           *
         *         *
           *     *
              *

    SLOPE:
             *
            *
           *
          *
         *
        *
       *
      * * * * * * * *

The Renode-emulated board is fed data that the application
recognizes as a series of alternating ring and slope gestures.

Modifying Sample for Your Own Project
*************************************

It is recommended that you copy and modify one of the two TensorFlow
samples when creating your own TensorFlow project. To build with
TensorFlow, you must enable the below Kconfig options in your :file:`prj.conf`
and set a flag in your :file:`CMakeLists.txt`.

:file:`prj.conf`:

.. code-block:: console

    CONFIG_CPLUSPLUS=y
    CONFIG_NEWLIB_LIBC=y
    CONFIG_TENSORFLOW=y

:file:`CMakeLists.txt`:

.. code-block:: console

    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fno-threadsafe-statics")

Training
********
Follow the instructions in the :file:`train/` directory to train your
own model for use in the sample.
