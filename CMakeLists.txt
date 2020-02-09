cmake_minimum_required(VERSION 3.0)
project (smp_bench)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake OPTIONAL RESULT_VARIABLE HAS_CONAN)
if (NOT HAS_CONAN)
  message(FATAL_ERROR "missing conanbuildinfo.cmake did you forget to run conan install ?")
endif()
conan_basic_setup()

include(os)

set(SOURCES
    service.cpp # ...add more here
)

os_add_executable(smp_bench "SMP test" ${SOURCES})
os_add_stdout(smp_bench default_stdout)
os_add_drivers(smp_bench boot_logger)
#os_add_plugins(smp_bench vfs)
