# Copyright (c) 2023 Intel Corporation.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(llext_test)

add_subdirectory(hello_world)
# add_subdirectory(hello_phil)

target_sources(app PRIVATE src/test_llext_simple.c)

target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/include
  ${ZEPHYR_BASE}/kernel/include
  ${ZEPHYR_BASE}/arch/${ARCH}/include
  )

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

generate_inc_file_for_target(
  app
  ${HELLO_WORLD_LLEXT}
  ${gen_dir}/hello_world.inc
  )

add_dependencies(app hello_world)

# generate_inc_file_for_target(
#   app
#   ${HELLO_PHIL_LLEXT}
#   ${gen_dir}/hello_phil.inc
#   )

# add_dependencies(app hello_phil)

# generate_inc_file_for_target(
#   app
#   ${PHILOSOPHERS_LLEXT}
#   ${gen_dir}/philosophers.inc
#   )

# add_dependencies(app philosophers)
