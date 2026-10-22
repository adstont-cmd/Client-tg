# Builds the example mod: a shared library next to mod.json.
#   cmake -B build && cmake --build build --config Release
# Then copy mod.json and the library into <Telegram folder>/mods/hello/.
cmake_minimum_required(VERSION 3.16)
project(hello_mod CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

add_library(hello SHARED hello.cpp)
target_include_directories(hello PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../modapi/include)
# The same file name on every platform but Windows: libhello.so / .dylib.
if (MSVC)
    # One self-contained dll, no dependency on a particular MSVC runtime.
    set_property(TARGET hello PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
