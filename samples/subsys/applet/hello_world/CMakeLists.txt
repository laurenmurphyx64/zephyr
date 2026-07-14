# Copyright (c) 2026 Intel Corporation
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(applet_hello_world LANGUAGES C)

set(ext_src  ${PROJECT_SOURCE_DIR}/ext/hello_world_ext.c)

if(CONFIG_LLEXT)
  # ──────────────────────────────────────────────────
  # Build the loadable extension (hello_world_ext.c → hello_world.llext)
  # and embed it as a generated C header (hello_world.inc).
  # ──────────────────────────────────────────────────
  set(ext_bin  ${PROJECT_BINARY_DIR}/llext/hello_world.llext)
  set(ext_inc  ${ZEPHYR_BINARY_DIR}/include/generated/hello_world.inc)

  add_llext_target(hello_world_ext
    OUTPUT  ${ext_bin}
    SOURCES ${ext_src}
  )
  generate_inc_file_for_target(app ${ext_bin} ${ext_inc})
else()
target_sources(app PRIVATE ${ext_src})
endif()

# ──────────────────────────────────────────────────
# Host application
# ──────────────────────────────────────────────────
target_sources(app PRIVATE src/main.c)
