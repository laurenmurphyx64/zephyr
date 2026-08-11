# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(applet_shell_loader LANGUAGES C)

target_sources(app PRIVATE src/main.c)

# Add a target for building the applet extension code
set(ext_src "${PROJECT_SOURCE_DIR}/hello_world.c")
set(ext_bin "${PROJECT_BINARY_DIR}/hello_world.llext")
add_llext_target(hello_world_ext
  OUTPUT  "${ext_bin}"
  SOURCES "${ext_src}"
)
