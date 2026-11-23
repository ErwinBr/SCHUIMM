# Selects exactly one native platform backend at configure time.
#
#   SCHUIMM_PLATFORM       Windows | macOS | Linux (detected, not user-settable)
#   SCHUIMM_LINUX_BACKEND  X11 (default). Wayland is reserved for a future backend.

if(WIN32)
    set(SCHUIMM_PLATFORM "Windows")
elseif(APPLE)
    set(SCHUIMM_PLATFORM "macOS")
elseif(UNIX)
    set(SCHUIMM_PLATFORM "Linux")
    set(SCHUIMM_LINUX_BACKEND "X11" CACHE STRING "Linux windowing backend")
    set_property(CACHE SCHUIMM_LINUX_BACKEND PROPERTY STRINGS X11)
    if(NOT SCHUIMM_LINUX_BACKEND STREQUAL "X11")
        message(FATAL_ERROR "SCHUIMM_LINUX_BACKEND='${SCHUIMM_LINUX_BACKEND}' is not implemented. Only X11 is available.")
    endif()
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "Schuimm platform backend: ${SCHUIMM_PLATFORM}${SCHUIMM_LINUX_BACKEND}")
