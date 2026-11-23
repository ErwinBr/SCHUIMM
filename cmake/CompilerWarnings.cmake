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
endfunction()
