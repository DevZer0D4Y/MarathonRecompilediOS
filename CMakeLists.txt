cmake_minimum_required (VERSION 3.20)

if(NOT DEFINED ENV{VCPKG_ROOT})
    message(FATAL_ERROR "VCPKG_ROOT is not defined!")
endif()

set(MARATHON_RECOMP_THIRDPARTY_ROOT ${CMAKE_SOURCE_DIR}/thirdparty)
set(MARATHON_RECOMP_TOOLS_ROOT ${CMAKE_SOURCE_DIR}/tools)
set(CMAKE_CXX_STANDARD 20)
set(BUILD_SHARED_LIBS OFF)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MARATHON_RECOMP_HOST_TOOLS_ONLY "Build native recompilers without the game runtime" OFF)
set(MARATHON_RECOMP_HOST_TOOLS_DIR "" CACHE PATH "Directory containing native Mac build tools")
set(MARATHON_RECOMP_METAL_SDK "macosx" CACHE STRING "SDK used by the native shader recompiler")
set(MARATHON_RECOMP_GENERATED_DIR "${CMAKE_SOURCE_DIR}/MarathonRecompLib" CACHE PATH "Generated CPU and shader sources")
set(MARATHON_RECOMP_GAME_INPUT_DIR "${CMAKE_SOURCE_DIR}/MarathonRecompLib/private" CACHE PATH "Read-only original game input directory or extracted game root")
include("${CMAKE_SOURCE_DIR}/cmake/GameInputs.cmake")

project("MarathonRecomp-ALL")
set(MARATHON_RECOMP_IOS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(MARATHON_RECOMP_IOS ON)
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "The iOS port currently supports arm64 devices only")
    endif()
endif()

if (APPLE)
    enable_language(OBJC OBJCXX)
endif()

if (CMAKE_OSX_ARCHITECTURES)
    set(MARATHON_RECOMP_ARCHITECTURE ${CMAKE_OSX_ARCHITECTURES})
elseif(CMAKE_SYSTEM_PROCESSOR)
    set(MARATHON_RECOMP_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
else()
    set(MARATHON_RECOMP_ARCHITECTURE ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif()
string(TOLOWER "${MARATHON_RECOMP_ARCHITECTURE}" MARATHON_RECOMP_ARCHITECTURE)
message(STATUS "Detected architecture: ${MARATHON_RECOMP_ARCHITECTURE}")

if (MARATHON_RECOMP_ARCHITECTURE STREQUAL "x86_64" OR MARATHON_RECOMP_ARCHITECTURE STREQUAL "amd64")
    # Target Sandy Bridge for all projects
    add_compile_options(
        -march=sandybridge
    )
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Normally only defined by Visual Studio, added for consistency
    add_compile_definitions(_DEBUG)
endif()

if(MARATHON_RECOMP_HOST_TOOLS_ONLY OR MARATHON_RECOMP_IOS)
    add_subdirectory(${MARATHON_RECOMP_TOOLS_ROOT})
    if(NOT MARATHON_RECOMP_HOST_TOOLS_ONLY)
        add_subdirectory(${MARATHON_RECOMP_THIRDPARTY_ROOT})
    endif()
else()
    add_subdirectory(${MARATHON_RECOMP_THIRDPARTY_ROOT})
    add_subdirectory(${MARATHON_RECOMP_TOOLS_ROOT})
endif()

# Include sub-projects.
add_subdirectory("MarathonRecompLib")
if(NOT MARATHON_RECOMP_HOST_TOOLS_ONLY)
    add_subdirectory("MarathonRecomp")
endif()
