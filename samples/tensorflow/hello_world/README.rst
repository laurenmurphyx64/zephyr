.. tensorflow_hello_world:

TensorFlow Hello World
######################

Overview
********

A simple TensorFlow sample that replicates a sine wave and
is designed to demonstrate the absolute basics of using
TensorFlow Lite Micro. It includes the full end-to-end workflow
of training a model, converting it for use with TensorFlow Lite 
for Microcontrollers for running inference on a microcontroller.

The model is trained to replicate a sine function and generates
x values to print alongside the y values predicted by the model.
The x values iterate from 0 to an approximation of 2 *  Pi.

.. Note::
   This README and sample have been modified from 
   `the TensorFlow Hello World sample for Zephyr`_.

.. _the TensorFlow Hello World sample for Zephyr:
   https://github.com/tensorflow/tensorflow/tree/master/tensorflow/lite/micro/examples/hello_world

Building and Running
********************

This sample should work on most boards since it does not rely
on any sensors.

This application can be built and executed on QEMU as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/tensorflow/hello_world
   :host-os: unix
   :board: qemu_x86
   :goals: run
   :compact:

Exit QEMU by pressing :kbd:`CTRL+A` :kbd:`x`.

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

The modified sample prints 50 generated-x-and-predicted-y pairs.

Training
********
Follow the instructions in the `train/` directory to train your
own model for use in the sample.