cmake_minimum_required(VERSION 3.24)
project(testdrive_enhanced C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL3 REQUIRED CONFIG)

file(GLOB_RECURSE TD_SOURCES CONFIGURE_DEPENDS src/*.c)
file(GLOB_RECURSE TD_ENHANCED_SOURCES CONFIGURE_DEPENDS src/enhanced/*.c)

add_executable(testdrive-enhanced ${TD_SOURCES})
target_include_directories(testdrive-enhanced PRIVATE src)
target_link_libraries(testdrive-enhanced PRIVATE SDL3::SDL3)

if(MSVC)
    target_compile_options(testdrive-enhanced PRIVATE /W3)
else()
    # The memory model reads multi-byte values through byte pointers (see src/mem.h).
    target_compile_options(testdrive-enhanced PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)
    # The renderer does per-pixel work every frame; keep it optimised in Debug builds.
    set_source_files_properties(${TD_ENHANCED_SOURCES} PROPERTIES COMPILE_OPTIONS -O2)
endif()

if(WIN32)
    add_custom_command(TARGET testdrive-enhanced POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:testdrive-enhanced> $<TARGET_FILE_DIR:testdrive-enhanced>
        COMMAND_EXPAND_LISTS)
endif()
