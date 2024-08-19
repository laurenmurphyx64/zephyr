.. _mmc56x3:

MMC56X3 Temperature and Magnetometer Sensor
###########################################

Overview
********

This sample shows how to use the `Memsic MMC56X3`_ environmental sensor.
The sample periodically reads temperature and magnetometer data from the
first available MMC56X3 device discovered in the system. The sensor operates in
polling mode, but it can be configured to operate in continuous mode.

.. _Memsic MMC56X3:
   https://learn.adafruit.com/adafruit-mmc5603-triple-axis-magnetometer

Building and Running
********************

The sample can be configured to support MMC56X3 sensors connected via I2C.
Configuration is done via :ref:`devicetree <dt-guide>`. The devicetree
must have an enabled node with ``compatible = "memsic,mmc56x3";``. See
:dtcompatible:`memsic,mmc56x3` for the devicetree binding and see below for
examples and common configurations.

MMC56X3 via Arduino I2C pins
============================

If you wired the sensor to an I2C peripheral on an Arduino header, build and
flash with:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/mmc56x3
   :goals: build flash
   :gen-args: -DDTC_OVERLAY_FILE=arduino_i2c.overlay

The Devicetree overlay :zephyr_file:`samples/sensor/mmc56x3/arduino_i2c.overlay`
works on any board with a properly configured Arduino pin-compatible I2C
peripheral.

Board-specific Overlays
=======================

If your board's devicetree does not have a MMC56X3 node already, you can create
a board-specific devicetree overlay adding one in the :file:`boards` directory.
See existing overlays for examples.

The build system uses these overlays by default when targeting those boards, so
no ``DTC_OVERLAY_FILE`` setting is needed when building and running.

Sample Output
=============

The sample prints output to the serial console. MMC56X3 device driver messages
are also logged. Refer to your board's documentation for information on
connecting to its serial console.

Here is example output for the default application settings, assuming that only
one MMC56X3 sensor is connected to the standard Arduino I2C pins:

.. code-block:: none

   [00:00:00.009,000] <err> FXOS8700: Could not get WHOAMI value
   [00:00:00.009,000] <dbg> MMC56X3: mmc56x3_chip_init: ID OK
   *** Booting Zephyr OS build v3.7.0-1326-g09c90895748e ***
   Found device "mmc56x3@30", getting sensor data
   mmc56x3@30: 16.406250 C
   mmc56x3@30: MX=0.463378 MY=0.481689 MZ=-0.449951
   mmc56x3@30: 16.406250 C
   mmc56x3@30: MX=0.454589 MY=0.483642 MZ=-0.443603
   mmc56x3@30: 16.406250 C
   mmc56x3@30: MX=0.458984 MY=0.484375 MZ=-0.438232
   mmc56x3@30: 16.406250 C
   mmc56x3@30: MX=0.455566 MY=0.479003 MZ=-0.429992
   mmc56x3@30: 16.406250 C
   mmc56x3@30: MX=0.457275 MY=0.482177 MZ=-0.438537
   mmc56x3@30: 16.406250 C

Several attributes of the sensor described in :dtcompatible:`memsic,mmc56x3`
can be set in Devicetree and with :c:func:`sensor_attr_set`. One of these
is the sensor's continuous mode, enabled by setting the ODR to a non-zero
number less than or equal to 1000:

.. code-block:: devicetree

   &arduino_i2c {
      status = "okay";
      mmc56x3: mmc56x3@30 {
         compatible = "memsic,mmc56x3";
         reg = <0x30>;
         magn-odr = <128>;
         auto-self-reset;
      };
   };

Here is example output:

.. code-block:: none

   [00:00:00.009,000] <err> FXOS8700: Could not get WHOAMI value
   [00:00:00.009,000] <dbg> MMC56X3: mmc56x3_chip_init: ID OK
   *** Booting Zephyr OS build v3.7.0-1326-g09c90895748e ***
   Found device "mmc56x3@30", getting sensor data
   mmc56x3@30: -75 C
   mmc56x3@30: MX=0.441466 MY=0.476561 MZ=-0.378844
   mmc56x3@30: -75 C
   mmc56x3@30: MX=-0.219176 MY=-0.502867 MZ=-0.546141
   mmc56x3@30: -75 C
   mmc56x3@30: MX=-0.213927 MY=-0.502989 MZ=-0.543882
   mmc56x3@30: -75 C
   mmc56x3@30: MX=-0.218383 MY=-0.501463 MZ=-0.546812
   mmc56x3@30: -75 C
   mmc56x3@30: MX=-0.219176 MY=-0.502928 MZ=-0.548277
   mmc56x3@30: -75 C

Temperature cannot be read in continuous mode, so it is -75 (the minimum) in the output.

For more information about continuous mode and the affects of attributes like
ODR on the sampling rate, refer to page 8 of the `MMC56X3 datasheet`_.

.. _MMC56X3 datasheet:
   https://media.digikey.com/pdf/Data%20Sheets/MEMSIC%20PDFs/MMC5603NJ_RevB_7-12-18.pdf
