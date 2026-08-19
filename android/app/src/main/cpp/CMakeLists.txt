cmake_minimum_required(VERSION 3.22)
project(swiv C CXX)
set(SWIV_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
set(RAYLIB_SRC /home/jon/raylib-5.5-src)
if(NOT EXISTS ${RAYLIB_SRC}/CMakeLists.txt)
  set(RAYLIB_SRC /home/jon/raylib-src)
endif()
set(LIBXMP_SRC /home/jon/libxmp-src)

# raylib for Android (builds android_native_app_glue itself)
set(PLATFORM "Android" CACHE STRING "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
add_subdirectory(${RAYLIB_SRC} raylib)

# libxmp static
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(LIBXMP_PIC ON CACHE BOOL "" FORCE)
set(LIBXMP_DISABLE_DEPACKERS ON CACHE BOOL "" FORCE)
set(LIBXMP_DISABLE_PROWIZARD ON CACHE BOOL "" FORCE)
add_subdirectory(${LIBXMP_SRC} libxmp)

file(GLOB SWIV_SOURCES
  ${SWIV_ROOT}/src/viewer.c ${SWIV_ROOT}/src/audio.c ${SWIV_ROOT}/src/sfx_bank.c ${SWIV_ROOT}/src/swivdata.c ${SWIV_ROOT}/src/frontend.c
  ${SWIV_ROOT}/src/engine/*.c ${SWIV_ROOT}/src/behaviours/*.c)
add_library(swiv SHARED ${SWIV_SOURCES})
target_include_directories(swiv PRIVATE ${SWIV_ROOT}/src ${RAYLIB_SRC}/src ${LIBXMP_SRC}/include ${ANDROID_NDK}/sources/android/native_app_glue)
target_compile_definitions(swiv PRIVATE _GNU_SOURCE PLATFORM_ANDROID)
target_compile_options(swiv PRIVATE -O2 -std=gnu11 -Wno-unused-parameter)
target_link_options(swiv PRIVATE -u ANativeActivity_onCreate)
target_link_libraries(swiv raylib xmp_static android EGL GLESv2 OpenSLES log m)
