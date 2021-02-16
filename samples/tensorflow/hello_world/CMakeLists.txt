cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tensorflow_lite_micro_hello_world)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -std=c++11 -fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-unwind-tables -ffunction-sections -fdata-sections -fmessage-length=0 -DTF_LITE_STATIC_MEMORY -DTF_LITE_DISABLE_X86_NEON -O3 -Werror -Wsign-compare -Wdouble-promotion -Wshadow -Wunused-variable -Wmissing-field-initializers -Wunused-function -Wswitch -Wvla -Wall -Wextra -Wstrict-aliasing -Wno-unused-parameter -DZEPHYR_VEXRISCV -fno-threadsafe-statics -Wno-sign-compare -Wno-narrowing")
set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -std=c11 -fno-unwind-tables -ffunction-sections -fdata-sections -fmessage-length=0 -DTF_LITE_STATIC_MEMORY -DTF_LITE_DISABLE_X86_NEON -O3 -Werror -Wsign-compare -Wdouble-promotion -Wshadow -Wunused-variable -Wmissing-field-initializers -Wunused-function -Wswitch -Wvla -Wall -Wextra -Wstrict-aliasing -Wno-unused-parameter -DZEPHYR_VEXRISCV")
set(CMAKE_EXE_LINKER_FLAGS "-Wl,--fatal-warnings -Wl,--gc-sections -lm")

target_sources(app PRIVATE
		src/assert.cc
		src/main.cc
		src/main_functions.cc
		src/constants.cc
		src/output_handler.cc
		src/model.cc
		src/model.h
		src/output_handler.h
		src/constants.h
		src/main_functions.h
		)