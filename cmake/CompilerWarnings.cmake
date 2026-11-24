# Shared warning configuration for every Schuimm target.
option(SCHUIMM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

function(schuimm_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
        if(SCHUIMM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
            -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion)
        if(SCHUIMM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    if(APPLE)
        # macOS 10.13 is the minimum: calling an API introduced later without an availability
        # check is an error even when warnings are not treated as errors.
        target_compile_options(${target} PRIVATE -Werror=unguarded-availability-new)
    endif()
endfunction()
