# Vendored third-party C libraries (see README.md for versions, sources and licences).
#
# Built with our own minimal CMake instead of the upstream build systems so the result is the
# same on every platform and nothing is configured or downloaded at build time. The upstream
# code is compiled without the engine's warning flags (and with its own warnings silenced):
# it is not ours to fix, and -Werror must not depend on upstream code.

function(schuimm_third_party_target target)
    set_target_properties(${target} PROPERTIES C_STANDARD 99 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(${target} PRIVATE /w)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

# zlib 1.3.2: only the in-memory compression/decompression sources (no gz* file API).
add_library(schuimm_zlib STATIC
    zlib/adler32.c
    zlib/compress.c
    zlib/crc32.c
    zlib/deflate.c
    zlib/infback.c
    zlib/inffast.c
    zlib/inflate.c
    zlib/inftrees.c
    zlib/trees.c
    zlib/uncompr.c
    zlib/zutil.c)
add_library(Schuimm::ThirdParty::ZLIB ALIAS schuimm_zlib)
target_include_directories(schuimm_zlib SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/zlib")
schuimm_third_party_target(schuimm_zlib)

# libpng 1.6.59 with the upstream default configuration (scripts/pnglibconf.h.prebuilt, copied
# as pnglibconf.h). Hardware-specific filter optimisations are disabled: they need extra,
# per-architecture source files, and decoding small game assets does not need them.
add_library(schuimm_png STATIC
    libpng/png.c
    libpng/pngerror.c
    libpng/pngget.c
    libpng/pngmem.c
    libpng/pngpread.c
    libpng/pngread.c
    libpng/pngrio.c
    libpng/pngrtran.c
    libpng/pngrutil.c
    libpng/pngset.c
    libpng/pngtrans.c
    libpng/pngwio.c
    libpng/pngwrite.c
    libpng/pngwtran.c
    libpng/pngwutil.c)
add_library(Schuimm::ThirdParty::PNG ALIAS schuimm_png)
target_include_directories(schuimm_png SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libpng")
target_compile_definitions(schuimm_png PRIVATE
    PNG_ARM_NEON_OPT=0
    PNG_INTEL_SSE_OPT=0
    PNG_LOONGARCH_LSX_OPT=0
    PNG_MIPS_MMI_OPT=0
    PNG_MIPS_MSA_OPT=0
    PNG_POWERPC_VSX_OPT=0
    PNG_RISCV_RVV_OPT=0)
target_link_libraries(schuimm_png PUBLIC schuimm_zlib)
if(UNIX AND NOT APPLE)
    target_link_libraries(schuimm_png PUBLIC m)
endif()
schuimm_third_party_target(schuimm_png)
