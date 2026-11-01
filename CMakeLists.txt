cmake_minimum_required(VERSION 3.16)
project(curl-ui VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(CURLUI_BUILD_GUI "Build the SDL2 application" ON)
option(CURLUI_SANITIZE "Build with ASan and UBSan" OFF)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
endif()
if(MSVC)
  # static CRT, so the installer needs no redistributable
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  add_compile_options(/utf-8 /W3)
else()
  add_compile_options(-Wall -Wextra)
endif()
if(CURLUI_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

# ---- core: no SDL, unit tested ----
add_library(curlui_core STATIC
  src/core/options.c src/core/options_data.c src/core/builder.c
  src/core/process.c src/core/outbuf.c src/core/json_fmt.c
  src/core/curlinfo.c src/core/docs.c)
target_include_directories(curlui_core PUBLIC src/core)
target_link_libraries(curlui_core PUBLIC Threads::Threads)
if(NOT WIN32)
  # posix_spawn, nanosleep, and strdup-free C11 stay available
  target_compile_definitions(curlui_core PRIVATE _DEFAULT_SOURCE)
endif()

# Regenerate from curl's docs: cmake --build build --target regen-options
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_custom_target(regen-options
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_options.py
    COMMENT "Regenerating src/core/options_data.c")
endif()

enable_testing()
add_executable(echoargs tests/echoargs.c)
if(WIN32)
  target_link_libraries(echoargs PRIVATE shell32)
endif()
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE curlui_core)
set(CURLUI_TEST_CURL "curl" CACHE STRING "curl used by the end-to-end test")
add_test(NAME core COMMAND test_core $<TARGET_FILE:echoargs> ${CURLUI_TEST_CURL})
