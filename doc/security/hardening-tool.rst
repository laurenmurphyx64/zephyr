.. _hardening:

Hardening Tool
##############

```
TODO: 
- Update documentation to fix spelling / wording
- Add discussion of what hardening is to documentation (see https://linux-audit.com/linux-server-hardening-most-important-steps-to-secure-systems/)
- Update options in case experimental options have been moved to better support
- Add mention / link to hardenconfig to menuconfig.rst (or somewhere else)
- Add option to hardenconfig
- Update with new security features / remove any obsolete + discontinued options (latter kind of done)
- Add experimental flag? e.g. crypto or explain that a feature, such a crypto drivers, which may seem beneficial to security, may be set to n as the feature is currently experimental
- find a way to do pattern matching, i.e. turn off all *DBG and *DEBUG Kconfig options and all *STATS options, *SHELL options and *TEST options
```

// What is hardening?

Zephyr contains several optional features that make the overall system
more secure. As we take advantage of hardware features, many of these
options are platform specific and besides it, some of them are unknown
by developers.

To address this problem, Zephyr provides a tool that helps to check an
application configuration option list against a list of hardening
preferences defined by the Zephyr Security Subcommittee. This tool can identify the build
target and based on that provides suggestions and recommendations on how to
optimize the configuration for security.

Usage
*****

After configure of your application, change directory to the build folder and:

.. code-block:: console

   # ninja build system:
   $ ninja hardenconfig
   # make build system:
   $ make hardenconfig

The output should be similar to the one below:

.. code-block:: console


                          name                       |   current   |    recommended     ||        check result
   ===================================================================================================================
   CONFIG_HW_STACK_PROTECTION                        |      n      |         y          ||            FAIL
   CONFIG_BOOT_BANNER                                |      y      |         n          ||            FAIL
   CONFIG_PRINTK                                     |      y      |         n          ||            FAIL
   CONFIG_EARLY_CONSOLE                              |      y      |         n          ||            FAIL
   CONFIG_OVERRIDE_FRAME_POINTER_DEFAULT             |      n      |         y          ||            FAIL
   CONFIG_DEBUG_INFO                                 |      y      |         n          ||            FAIL
   CONFIG_TEST_RANDOM_GENERATOR                      |      y      |         n          ||            FAIL
   CONFIG_BUILD_OUTPUT_STRIPPED                      |      n      |         y          ||            FAIL
   CONFIG_STACK_SENTINEL                             |      n      |         y          ||            FAIL

`hardenconfig` selects and displays only those settings applicable to your current project. You can find a list of all recommended Kconfig settings in the file :file:`scripts/kconfig/hardened.csv`.

// Discussio
