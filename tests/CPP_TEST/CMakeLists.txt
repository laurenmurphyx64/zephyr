cmake_minimum_required(VERSION 3.16)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})


project(ztest_framework_cpp)

file(GLOB test_sources src/*.cpp)

target_sources(app PRIVATE
	${test_sources}
)
