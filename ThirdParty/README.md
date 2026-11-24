# Third-party code

Vendored source code, built by [`CMakeLists.txt`](CMakeLists.txt) as static libraries. Only the
files needed for the build are kept; nothing is downloaded or configured at build time. Upstream
files are unmodified except where noted.

| Library | Version | Used by | Licence | Source archive (SHA-256) |
|---|---|---|---|---|
| zlib | 1.3.2 | libpng | zlib licence ([`zlib/LICENSE`](zlib/LICENSE)) | [`zlib-1.3.2.tar.gz`](https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz) (`bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`, matches the digest published with the GitHub release) |
| libpng | 1.6.59 | `Source/Resource/Image/PngDecoder.cpp` | PNG Reference Library License v2 ([`libpng/LICENSE`](libpng/LICENSE)) | [`v1.6.59` tag archive](https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.59.tar.gz) (`2540302a1844ad2b2b501977abecfa850f265f97b78f065a712ab4074a89f5b5`, computed on download; no digest is published for tag archives) |

## What was taken

* **zlib**: the compression and decompression sources (`adler32.c`, `compress.c`, `crc32.c`,
  `deflate.c`, `infback.c`, `inffast.c`, `inflate.c`, `inftrees.c`, `trees.c`, `uncompr.c`,
  `zutil.c`) and their headers. `gzguts.h` is included by `zutil.c`; the `gz*` file API itself is
  not built.
* **libpng**: the library sources (`png*.c` except `pngtest.c` and `example.c`) and headers.
  `pnglibconf.h` is upstream's `scripts/pnglibconf.h.prebuilt`, renamed (the default
  configuration, which includes the simplified `png_image` API).

## Build settings

* Compiled as C99 with the compiler's warnings off; the engine's `-Werror`/`/WX` flags are not
  applied to third-party code.
* libpng's hardware filter optimisations (ARM NEON, Intel SSE, MIPS, PowerPC, LoongArch, RISC-V)
  are disabled with `PNG_*_OPT=0`: they need additional per-architecture sources, and decoding
  small game assets does not need them.
* Only the resource layer links them, privately: engine headers never include `png.h` or
  `zlib.h`.

## Updating

1. Download the new release archives and verify them (zlib publishes digests and signatures).
2. Replace the files listed above, keeping the licence files.
3. Update the versions and checksums in this file and in the comments in `CMakeLists.txt`.
4. Run the full test suite; `Tests/Resource/ImageDecoderTests.cpp` covers every PNG colour type.
