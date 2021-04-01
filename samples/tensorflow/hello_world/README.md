# Hello World Example

This example is designed to demonstrate the absolute basics of using [TensorFlow
Lite for Microcontrollers](https://www.tensorflow.org/lite/microcontrollers) on Zephyr.
It includes the full end-to-end workflow of training a model, converting it for
use with TensorFlow Lite for Microcontrollers for running inference on a
microcontroller.

The model is trained to replicate a `sine` function and generates x values to print
alongside the y values predicted by the model. The x values iterate from 0 to an
approximation of 2 *  Pi.

(Note: this README and sample have been modified from the [sample in the TensorFlow tree](https://github.com/tensorflow/tensorflow/blob/master/tensorflow/lite/micro/examples/hello_world/).)

## Table of contents

-   [Enabling TensorFlow](#enabling-tensorflow)
-   [Deploy to QEMU](#deploy-to-qemu)
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

## Deploy to QEMU

To build and run on QEMU x86:

```
west build -b qemu_x86 -t run
```

To build and run on QEMU x86_64:

```
west build -b qemu_x86_64 -t run
```

Your output should be similar to the following:

```
To exit from QEMU enter: 'CTRL+a, x'[QEMU] CPU: qemu32,+nx,+pae
SeaBIOS (version zephyr-v1.0.0-0-g31d4e0e-dirty-20200714_234759-fv-az50-zephyr)
Booting from ROM..*** Booting Zephyr OS build zephyr-v2.5.0-648-gcdf62a023bbf  ***
x_value: 1.0*2^-127, y_value: -1.0849024*2^-5
x_value: 1.2566366*2^-2, y_value: 1.4578377*2^-2
x_value: 1.2566366*2^-1, y_value: 1.1527088*2^-1
x_value: 1.8849551*2^-1, y_value: 1.6782086*2^-1
x_value: 1.2566366*2^0, y_value: 1.9324824*2^-1
x_value: 1.5707957*2^0, y_value: 1.0340478*2^0
x_value: 1.8849551*2^0, y_value: 1.8477248*2^-1
x_value: 1.0995567*2^1, y_value: 1.6951603*2^-1
x_value: 1.2566366*2^1, y_value: 1.1527088*2^-1
x_value: 1.4137159*2^1, y_value: 1.1527088*2^-2
x_value: 1.5707957*2^1, y_value: -1.0849024*2^-6
...
```

Hit `Ctrl+A X` to exit QEMU.

## Deploy to Litex VexRiscv on Renode

Renode is an emulator that can be used to run the sample application on an emulated
Litex VexRiscv board.

To build for the LiteX VexRiscv board:

```
west build -b litex_vexriscv .
```

Download the Renode package for your OS from their [GitHub releases](https://github.com/renode/renode/releases/tag/v1.11.0)
and install it following the instructions in their [GitHub README](https://github.com/renode/renode/blob/master/README.rst).
Make sure you have the correct version of `mono` installed.

To run on Renode:

```
renode -e "set zephyr_elf @./build/zephyr/zephyr.elf; s @../renode/litex-vexriscv-tflite.resc"
```

Your output should be similar to the following:

```
*** Booting Zephyr OS build zephyr-v2.5.0-648-gcdf62a023bbf  ***
x_value: 1.0*2^-127, y_value: -1.0849024*2^-5
x_value: 1.2566366*2^-2, y_value: 1.4578377*2^-2
x_value: 1.2566366*2^-1, y_value: 1.1527088*2^-1
x_value: 1.8849551*2^-1, y_value: 1.6782086*2^-1
x_value: 1.2566366*2^0, y_value: 1.9324824*2^-1
x_value: 1.5707957*2^0, y_value: 1.0340478*2^0
x_value: 1.8849551*2^0, y_value: 1.8477248*2^-1
x_value: 1.0995567*2^1, y_value: 1.6951603*2^-1
x_value: 1.2566366*2^1, y_value: 1.1527088*2^-1
x_value: 1.4137159*2^1, y_value: 1.1527088*2^-2
...
```

## Train your own model

So far you have used an existing trained model to run inference on microcontrollers.
If you wish to train your own model, follow the instructions given in the
[train/](train/) directory.

