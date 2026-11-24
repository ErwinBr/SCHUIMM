# Third-party code

Vendored source code, built by [`CMakeLists.txt`](CMakeLists.txt) as static libraries. Only the
files needed for the build are kept; nothing is downloaded or configured at build time. Upstream
files are unmodified except where noted.

| Library | Version | Used by | Licence | Source archive (SHA-256) |
|---|---|---|---|---|
| zlib | 1.3.2 | libpng | zlib licence ([`zlib/LICENSE`](zlib/LICENSE)) | [`zlib-1.3.2.tar.gz`](https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz) (`bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`, matches the digest published with the GitHub release) |
| libpng | 1.6.59 | `Source/Resource/Image/PngDecoder.cpp` | PNG Reference Library License v2 ([`libpng/LICENSE`](libpng/LICENSE)) | [`v1.6.59` tag archive](https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.59.tar.gz) (`2540302a1844ad2b2b501977abecfa850f265f97b78f065a712ab4074a89f5b5`, computed on download; no digest is published for tag archives) |
| FreeType | 2.15.0 | `Source/Resource/Font/FontFace.cpp` | FreeType License (FTL, BSD-style with an attribution clause; FreeType is dual-licensed FTL/GPLv2 and SCHUIMM uses it under the FTL) ([`freetype/LICENSE.TXT`](freetype/LICENSE.TXT), [`freetype/FTL.TXT`](freetype/FTL.TXT)) | [`freetype-2.15.0.tar.xz`](https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz) (`93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5`, computed on download; the `.sig` was downloaded but not verified because no GnuPG was available) |
| miniaudio | 0.11.26 | `Source/Audio/Miniaudio/MiniaudioAudioDevice.cpp` | public domain (Unlicense) or MIT No Attribution, at the user's choice ([`miniaudio/LICENSE`](miniaudio/LICENSE)) | [`miniaudio.h` at tag `0.11.26`](https://raw.githubusercontent.com/mackron/miniaudio/0.11.26/miniaudio.h) (`6b376c7fbb1352ba1e0e432ab66069b335ea9405e45cb6c30b97ef0f3c9c3e0e`) and `LICENSE` (`457f1b500e0adf6bc059edddfa78a2f62012e7c3bb43476c20e0bd23b25ba0eb`), computed on download |

Demo asset (not compiled; shipped with the EngineDemo and used by the font tests):

| Asset | Licence | Source (SHA-256) |
|---|---|---|
| Noto Sans Regular, hinted TTF (`Applications/EngineDemo/Assets/Fonts/NotoSans-Regular.ttf`) | SIL Open Font License 1.1 ([`OFL.txt`](../Applications/EngineDemo/Assets/Fonts/OFL.txt), shipped next to the font) | [`fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf`](https://raw.githubusercontent.com/notofonts/notofonts.github.io/main/fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf) from the Noto project's distribution repository (`478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823`); `OFL.txt` from `notofonts/latin-greek-cyrillic` (`cee9892f9f0cc8fe882c9e9537ee6a89621d86ee7ceaf70b02e2b2b1c25c061a`) |

All four libraries are plain C without platform requirements beyond the C library. They are
compiled for macOS 10.13 with availability errors enabled, so a newer system API would fail the
build. miniaudio's CoreAudio backend links CoreFoundation, CoreAudio and AudioToolbox directly.

## What was taken

* **zlib**: the compression and decompression sources (`adler32.c`, `compress.c`, `crc32.c`,
  `deflate.c`, `infback.c`, `inffast.c`, `inflate.c`, `inftrees.c`, `trees.c`, `uncompr.c`,
  `zutil.c`) and their headers. `gzguts.h` is included by `zutil.c`; the `gz*` file API itself is
  not built.
* **libpng**: the library sources (`png*.c` except `pngtest.c` and `example.c`) and headers.
  `pnglibconf.h` is upstream's `scripts/pnglibconf.h.prebuilt`, renamed (the default
  configuration, which includes the simplified `png_image` API).

* **FreeType**: `include/` (public and internal headers) and the `.c`/`.h` files of
  `src/base`, `src/truetype`, `src/sfnt`, `src/smooth`, `src/autofit` and `src/psnames`, plus
  `LICENSE.TXT` and `docs/FTL.TXT`. Only these are compiled: `ftbase`, `ftbbox`, `ftbitmap`,
  `ftdebug`, `ftglyph`, `ftinit`, `ftmm`, `ftsystem`, and the `autofit`, `psnames`, `sfnt`,
  `smooth` and `truetype` modules.
  * Configuration is selected without editing upstream headers.
    `config/schuimm_ftmodule.h` (the module list) is passed as `FT_CONFIG_MODULES_H`, and
    `config/schuimm_ftoption.h` as `FT_CONFIG_OPTIONS_H`. The latter is a copy of
    `ftoption.h` with three changes marked `SCHUIMM`: zlib (gzip fonts) off, OT-SVG off, and
    `TT_CONFIG_OPTION_GPOS_KERNING` on. HarfBuzz, Brotli, bzip2 and libpng support stay off
    (upstream defaults).
  * **Local modification** of `src/sfnt/ttgpos.c`, marked `SCHUIMM`:
    * the basic GPOS kerning reader rejected the whole GPOS table when a lookup of the `kern`
      feature was not a pair adjustment (fonts such as Noto Sans also put contextual lookups
      there), which left those fonts without any kerning. Such lookups are now skipped;
    * the offset-collection pass skips them consistently, so the counted and collected subtables
      match.
    * Re-apply both changes when updating FreeType, or drop them if upstream changes the
      behaviour.
* **miniaudio**: `miniaudio.h` and `LICENSE`. `miniaudio.c` is ours: it defines
  `MINIAUDIO_IMPLEMENTATION` and includes the header. The configuration (`MA_NO_*` feature
  switches and the backend selection: WASAPI, DirectSound, CoreAudio, PulseAudio, ALSA, null) is
  a set of public compile definitions of the `schuimm_miniaudio` target.

## Build settings

* Compiled as C99 with the compiler's warnings off; the engine's `-Werror`/`/WX` flags are not
  applied to third-party code. With Clang the warnings are silenced with `-Wno-everything` and,
  on Apple platforms, `-Werror=unguarded-availability-new` is added (it would be silenced by
  `-w`), so third-party calls to APIs newer than macOS 10.13 fail the build.
* libpng's hardware filter optimisations (ARM NEON, Intel SSE, MIPS, PowerPC, LoongArch, RISC-V)
  are disabled with `PNG_*_OPT=0`: they need additional per-architecture sources, and decoding
  small game assets does not need them.
* Each library is linked privately by one engine library (libpng/zlib and FreeType by the
  resource layer, miniaudio by `schuimm_audio_miniaudio`): engine headers never include their
  headers.

## Updating

1. Download the new release archives and verify them (zlib publishes digests and signatures).
2. Replace the files listed above, keeping the licence files.
3. Update the versions and checksums in this file and in the comments in `CMakeLists.txt`.
4. Re-apply the marked local modifications (FreeType) and compare `schuimm_ftoption.h` with the
   new upstream `ftoption.h`.
5. Run the full test suite: `ImageDecoderTests` covers every PNG colour type, `FontFaceTests`
   covers loading, rasterisation and kerning, and `AudioTests`/`AudioDeviceSmoke` cover playback.
