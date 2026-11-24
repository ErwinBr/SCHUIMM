add_library(schuimm_test_framework STATIC TestFramework/TestMain.cpp)
target_include_directories(schuimm_test_framework PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/TestFramework")
target_compile_features(schuimm_test_framework PUBLIC cxx_std_23)
schuimm_configure_target(schuimm_test_framework)

# schuimm_add_test(<name> SOURCES ... LIBRARIES ... LABELS unit|integration|audio)
#
# Every test must say which category it belongs to, so CI can run each category explicitly:
#   unit         headless, no display, GPU or sound device needed; runs everywhere, including CI.
#   integration  needs a desktop session and an OpenGL 3.3 driver (Xvfb + Mesa on Linux CI).
#   audio        needs an audio output device (a PulseAudio null sink on Linux CI).
function(schuimm_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES;LABELS" ${ARGN})
    if(NOT ARG_LABELS)
        message(FATAL_ERROR "schuimm_add_test(${name}): LABELS unit|integration|audio is required")
    endif()
    foreach(label IN LISTS ARG_LABELS)
        if(NOT label MATCHES "^(unit|integration|audio)$")
            message(FATAL_ERROR "schuimm_add_test(${name}): unknown label '${label}' (use unit, integration or audio)")
        endif()
    endforeach()
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE schuimm_test_framework ${ARG_LIBRARIES})
    schuimm_configure_target(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "${ARG_LABELS}")
endfunction()

add_subdirectory(Core)
add_subdirectory(Platform)
add_subdirectory(Resource)
add_subdirectory(Graphics)
add_subdirectory(Audio)
add_subdirectory(Integration)
