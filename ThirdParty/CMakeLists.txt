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
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Silence upstream warnings, but keep the macOS 10.13 availability check an error:
        # -w would suppress it as well.
        target_compile_options(${target} PRIVATE -Wno-everything)
        if(APPLE)
            target_compile_options(${target} PRIVATE -Werror=unguarded-availability-new)
        endif()
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

# FreeType 2.15.0: TrueType driver, SFNT, anti-aliasing rasteriser, auto-hinter and PostScript
# glyph names only (see freetype/config/schuimm_ftmodule.h). The upstream configuration headers
# are left untouched; ours are selected with FT_CONFIG_MODULES_H / FT_CONFIG_OPTIONS_H, which
# must also be visible to code that includes the FreeType headers (hence PUBLIC).
add_library(schuimm_freetype STATIC
    freetype/src/base/ftbase.c
    freetype/src/base/ftbbox.c
    freetype/src/base/ftbitmap.c
    freetype/src/base/ftdebug.c
    freetype/src/base/ftglyph.c
    freetype/src/base/ftinit.c
    freetype/src/base/ftmm.c
    freetype/src/base/ftsystem.c
    freetype/src/autofit/autofit.c
    freetype/src/psnames/psnames.c
    freetype/src/sfnt/sfnt.c
    freetype/src/smooth/smooth.c
    freetype/src/truetype/truetype.c)
add_library(Schuimm::ThirdParty::FreeType ALIAS schuimm_freetype)
target_include_directories(schuimm_freetype SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/freetype/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/freetype/include")
target_compile_definitions(schuimm_freetype
    PUBLIC
        "FT_CONFIG_MODULES_H=<schuimm_ftmodule.h>"
        "FT_CONFIG_OPTIONS_H=<schuimm_ftoption.h>"
    PRIVATE
        FT2_BUILD_LIBRARY)
schuimm_third_party_target(schuimm_freetype)

# miniaudio 0.11.26: only the low-level device API (playback through the platform's audio
# backend). Decoding, encoding, the engine, node graph and resource manager are compiled out;
# SCHUIMM decodes and mixes audio itself (Source/Audio).
add_library(schuimm_miniaudio STATIC miniaudio/miniaudio.c)
add_library(Schuimm::ThirdParty::Miniaudio ALIAS schuimm_miniaudio)
target_include_directories(schuimm_miniaudio SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio")
target_compile_definitions(schuimm_miniaudio PUBLIC
    MA_NO_DECODING
    MA_NO_ENCODING
    MA_NO_ENGINE
    MA_NO_GENERATION
    MA_NO_NODE_GRAPH
    MA_NO_RESOURCE_MANAGER
    MA_ENABLE_ONLY_SPECIFIC_BACKENDS
    MA_ENABLE_WASAPI
    MA_ENABLE_DSOUND
    MA_ENABLE_COREAUDIO
    MA_ENABLE_PULSEAUDIO
    MA_ENABLE_ALSA
    MA_ENABLE_NULL)
if(APPLE)
    # Link the frameworks directly instead of loading them at run time, so the compiler checks
    # every CoreAudio call against the 10.13 deployment target.
    target_compile_definitions(schuimm_miniaudio PUBLIC MA_NO_RUNTIME_LINKING)
    target_link_libraries(schuimm_miniaudio PUBLIC
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
    # PulseAudio and ALSA are loaded at run time (dlopen): no build dependency on them.
    find_package(Threads REQUIRED)
    target_link_libraries(schuimm_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
schuimm_third_party_target(schuimm_miniaudio)

# nlohmann/json 3.12.0: header-only C++ JSON library (the single-header json.hpp), used privately
# by the room resource loader (Source/Room/RoomResourceLoader.cpp) only. The SYSTEM include
# keeps its warnings out of the engine's -Werror//WX build. Its std::filesystem integration is
# switched off: <filesystem> is unavailable on the macOS 10.13 deployment target.
add_library(schuimm_json INTERFACE)
add_library(Schuimm::ThirdParty::Json ALIAS schuimm_json)
target_include_directories(schuimm_json SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann/include")
target_compile_definitions(schuimm_json INTERFACE
    JSON_HAS_FILESYSTEM=0
    JSON_HAS_EXPERIMENTAL_FILESYSTEM=0
    JSON_NO_IO=1
    JSON_USE_IMPLICIT_CONVERSIONS=0)
