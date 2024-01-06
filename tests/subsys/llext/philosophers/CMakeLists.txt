# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(philosophers)

if(DEFINED DEBUG_PRINTF)
  zephyr_compile_definitions(DEBUG_PRINTF=${DEBUG_PRINTF})
endif()
if(DEFINED SAME_PRIO)
  zephyr_compile_definitions(SAME_PRIO)
endif()
if(DEFINED STATIC_OBJS)
  zephyr_compile_definitions(STATIC_OBJS)
endif()
if(DEFINED FORKS)
  zephyr_compile_definitions(FORKS=${FORKS})
endif()

# TODO check which architecture is being used
if(CONFIG_ARM)
    set(CMAKE_C_FLAGS "-mlong-calls" "-mthumb")

    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/philosophers.llext
	COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS} -c
		-I ${PROJECT_SOURCE_DIR}/../../../../include
		-imacros${PROJECT_BINARY_DIR}/../zephyr/include/generated/autoconf.h
		-o ${PROJECT_BINARY_DIR}/philosophers.llext
		${PROJECT_SOURCE_DIR}/main.c

endif()

set(PHILOSOPHERS_LLEXT ${PROJECT_BINARY_DIR}/philosophers.llext PARENT_SCOPE)

add_custom_target(philosophers DEPENDS ${PROJECT_BINARY_DIR}/philosophers.llext)