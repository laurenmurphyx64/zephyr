# Copyright (c) 2023 Intel Corporation.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_phil)

# TODO check which architecture is being used
if(CONFIG_ARM)
    set(CMAKE_C_FLAGS "-mlong-calls" "-mthumb")

    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/hello_phil.llext
	COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS} -c
		-I ${PROJECT_SOURCE_DIR}/../../../../include
		-I ${PROJECT_BINARY_DIR}/../zephyr/include/generated
		-imacros${PROJECT_BINARY_DIR}/../zephyr/include/generated/autoconf.h
		-o ${PROJECT_BINARY_DIR}/hello_phil.llext
		${PROJECT_SOURCE_DIR}/hello_phil.c
    )
elseif(CONFIG_XTENSA)
    set(CMAKE_C_FLAGS "-shared" "-fPIC" "-nostdlib" "-nodefaultlibs")

    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/hello_phil.llext
	COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS}
		-I ${PROJECT_SOURCE_DIR}/../../../../include
		-imacros${PROJECT_BINARY_DIR}/../zephyr/include/generated/autoconf.h
		-o ${PROJECT_BINARY_DIR}/hello_phil.pre.llext
		${PROJECT_SOURCE_DIR}/hello_phil.c
	COMMAND ${CROSS_COMPILE}strip -R .xt.*
		-o ${PROJECT_BINARY_DIR}/hello_phil.llext
		${PROJECT_BINARY_DIR}/hello_phil.pre.llext
    )
endif()

set(HELLO_PHIL_LLEXT ${PROJECT_BINARY_DIR}/hello_phil.llext PARENT_SCOPE)

add_custom_target(hello_phil DEPENDS ${PROJECT_BINARY_DIR}/hello_phil.llext)
