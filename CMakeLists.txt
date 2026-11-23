cmake_minimum_required(VERSION 3.25)

# Must be set before project() so that the toolchain picks it up.
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

project(Schuimm
    VERSION 0.1.0
    DESCRIPTION "Native foundation of a cross-platform 2D adventure game engine"
    LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep all executables next to each other so tests and the demo are easy to find.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(SCHUIMM_BUILD_TESTS "Build the automated tests" ON)
option(SCHUIMM_BUILD_DEMO "Build the EngineDemo application" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(PlatformSelection)

if(SCHUIMM_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(Source)

if(SCHUIMM_BUILD_DEMO)
    add_subdirectory(Applications/EngineDemo)
endif()

if(SCHUIMM_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
