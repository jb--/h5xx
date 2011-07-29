cmake_minimum_required(VERSION 2.8)

project(H5XX CXX)

if(CMAKE_BUILD_TYPE STREQUAL "${CMAKE_BUILD_TYPE_INIT}")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
    "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()
if(CMAKE_CXX_FLAGS STREQUAL "${CMAKE_CXX_FLAGS_INIT}")
  set(CMAKE_CXX_FLAGS "-fPIC -Wall" CACHE STRING
    "Flags used by the compiler during all build types." FORCE)
endif()

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# The FindBoost CMake module prefers multi-threaded libraries (filenames with
# postfix "-mt") over non-multi-threaded libraries. On Redhat or SuSE with
# installed system Boost libraries, this causes the system libraries (with
# "-mt") to override the custom-compiled libraries (without "-mt"). For now,
# H5XX requires custom-compiled libraries in any case, as upstream Boost does
# not contain the Boost.Log library yet.
#
if(NOT DEFINED Boost_USE_MULTITHREADED)
  set(Boost_USE_MULTITHREADED FALSE)
endif(NOT DEFINED Boost_USE_MULTITHREADED)

find_package(Boost 1.47.0 QUIET REQUIRED COMPONENTS unit_test_framework)
find_package(HDF5 QUIET REQUIRED)

include_directories("${HDF5_INCLUDE_DIR}")
include_directories("${CMAKE_SOURCE_DIR}")

enable_testing()
include(CTest)
add_subdirectory(test)