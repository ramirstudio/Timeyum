cmake_minimum_required(VERSION 3.20)
project(timeyum LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  # After Effects plug-ins are built against the static runtime.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Threads REQUIRED)

add_library(timeyum_core STATIC
  core/ColorSpace.cpp
  core/Fft.cpp
  core/Kernel.cpp
  core/Noise.cpp
  core/Parallel.cpp
  core/Timeyum.cpp
  core/Warp.cpp)
target_include_directories(timeyum_core PUBLIC core)
target_link_libraries(timeyum_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(timeyum_core PRIVATE /W3 /fp:precise)
else()
  target_compile_options(timeyum_core PRIVATE -Wall -Wextra)
endif()

add_executable(timeyum_cli tools/timeyum_cli.cpp)
target_link_libraries(timeyum_cli PRIVATE timeyum_core)

enable_testing()
add_executable(timeyum_test_fft tests/test_fft.cpp)
target_link_libraries(timeyum_test_fft PRIVATE timeyum_core)
add_test(NAME fft COMMAND timeyum_test_fft)
add_executable(timeyum_test_warp tests/test_warp.cpp)
target_link_libraries(timeyum_test_warp PRIVATE timeyum_core)
add_test(NAME warp COMMAND timeyum_test_warp)
# The wrapper logic (parameters, pixel conversion) runs against a mock of the AE SDK.
add_executable(timeyum_test_ae tests/test_ae_wrapper.cpp)
target_include_directories(timeyum_test_ae PRIVATE tests/ae_mock ae)
target_link_libraries(timeyum_test_ae PRIVATE timeyum_core)
add_test(NAME ae_wrapper COMMAND timeyum_test_ae)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME reference COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_reference.py $<TARGET_FILE:timeyum_cli>)
endif()

# After Effects plug-in: cmake -S . -B build -DAE_SDK_DIR="C:/path/to/AfterEffectsSDK/Examples"
if(AE_SDK_DIR AND WIN32)
  include(ae/CMakeLists.txt)
endif()
