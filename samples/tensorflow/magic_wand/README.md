# Magic wand example

This example shows how you can use TensorFlow Lite to run a 20 kilobyte neural
network model to recognize gestures with an accelerometer. It's designed to run
on systems with very small amounts of memory, such as microcontrollers.

(Note: this README and sample have been modified from the [sample in the TensorFlow tree](https://github.com/tensorflow/tensorflow/blob/master/tensorflow/lite/micro/examples/magic_wand/).)

## Table of contents

-   [Enabling TensorFlow](#enabling-tensorflow)
-   [Deploy to Litex VexRiscv on Renode](#deploy-to-litex-vexriscv-on-renode)
-   [Train your own model](#train-your-own-model)

## Enabling TensorFlow

Enable CONFIG_TENSORFLOW in `prj.conf`:

```
CONFIG_TENSORFLOW=y
```

Add the following flag to your `CMakeLists.txt`:

```
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fno-threadsafe-statics")
```

## Deploy to Litex VexRiscv on Renode

Renode is an emulator that can be used to run the sample application on an emulated Litex VexRiscv board. Renode feeds the data in [../renode/angle.data](../renode/angle.data) and [../renode/circle.data](../renode/circle.data) to the board's accelerometer.

To build for LiteX VexRiscv:

```
west build -b litex_vexriscv .
```

Download the Renode package for your OS from their [GitHub releases](https://github.com/renode/renode/releases/tag/v1.11.0) and install it following the instructions in their
[GitHub README](https://github.com/renode/renode/blob/master/README.rst). Make sure you have the correct version of `mono` installed.

To run on Renode:

```
renode -e "set zephyr_elf @./build/zephyr/zephyr.elf; s @../renode/litex-vexriscv-tflite.resc"
```

Your output should be similar to the following:

```
*** Booting Zephyr OS build zephyr-v2.5.0-649-gcdf8b0e8463e  ***
Got accelerometer, label: accel-0

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
```

(The pattern of ring and slope gestures repeats indefinitely.)

## Train your own model

To train your own model, or create a new model for a new set of gestures,
follow the instructions in [magic_wand/train/README.md](https://github.com/tensorflow/tensorflow/tree/master/tensorflow/lite/micro/examples/magic_wand/train/README.md).
