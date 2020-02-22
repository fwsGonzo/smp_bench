cmake_minimum_required(VERSION 3.0)
project (smp_bench)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake OPTIONAL RESULT_VARIABLE HAS_CONAN)
if (NOT HAS_CONAN)
  message(FATAL_ERROR "missing conanbuildinfo.cmake did you forget to run conan install ?")
endif()
conan_basic_setup()
include(os)

option(NETWORKED_SMP "Test SMP over the network" OFF)

if (NOT NETWORKED_SMP)
	set(SOURCES
    	service.cpp # ...add more here
	)
else()
	set(SOURCES
		netsmp.cpp
	)
endif()

os_add_executable(smp_bench "SMP test" ${SOURCES})
os_add_stdout(smp_bench default_stdout)
os_add_drivers(smp_bench vmxnet3)
os_add_plugins(smp_bench autoconf)
