# Text

Text rendering is split over three layers:

| Part | Library | Graphics API? |
|---|---|---|
| `FontFace`: font file loading, glyph lookup, rasterisation, metrics, kerning (FreeType) | `schuimm_resource` | no |
| `TextLayout`: UTF-8 decoding, measuring, line breaks, alignment, glyph placement | `schuimm_graphics` | no |
| `Font`: glyph cache and atlas pages on dynamic textures | `schuimm_graphics_resources` | through `Renderer2D` only |

## Unicode

All engine text is UTF-8. `Core/Text/Utf8.h` decodes it to code points (`utf8::decodeNext`):
* every ill-formed sequence becomes U+FFFD: overlong forms, surrogates, values above U+10FFFF,
  stray continuation bytes and truncated sequences;
* following Unicode's "maximal subpart" rule, so decoding never stops or skips valid text.

A character is a code point, never a byte. Glyph lookup uses the font's Unicode character map.
Any code point the font covers can be drawn, which includes all of Latin-1 and Latin Extended
in Noto Sans (é è ë ï ü ö ä ç ñ ß €, ...). There is no shaping: each code point maps to one
glyph, so scripts that need contextual forms (Arabic, Indic) or bidirectional layout are not
supported, and combining sequences are not composed.

## Fonts (`FontFace`)

```cpp
ResourceCache<FontFace> fonts(locator, loadFontFace);
auto face = fonts.load(ResourceId<FontFace>::parse("Fonts/NotoSans-Regular.ttf").value());
```

* Loads TrueType-outline fonts (`.ttf`, `.otf` with TrueType outlines, the first face of a
  `.ttc`) from memory; the bytes are kept for FreeType. Other data fails with `InvalidData`.
  CFF/PostScript-outline fonts need FreeType's CFF driver, which is not built.
* One FreeType library per face: no global state. Rasterising changes FreeType's state, so a
  face is used from one thread at a time.
* `rasterize(glyph, pixelSize)` produces an 8-bit coverage bitmap with bearings and advance.
  It uses FreeType's light auto-hinting, which keeps glyphs crisp at small sizes without
  distorting their shapes.
* `metrics(pixelSize)` gives ascender, descender and line height; `kerning(left, right, size)`
  the pair adjustment.
* **Kerning** comes from the legacy `kern` table or, with FreeType's
  `TT_CONFIG_OPTION_GPOS_KERNING` (enabled), from simple GPOS pair adjustment. Modern fonts such as
  Noto Sans keep their kerning only in GPOS. FreeType's basic GPOS reader rejected the whole table
  when the `kern` feature also contained contextual lookups. That is patched to skip such
  lookups (see [`ThirdParty/README.md`](../../ThirdParty/README.md)); class-based and contextual
  kerning beyond simple pairs is not applied.

## Glyph atlas (`Font`)

```cpp
auto font = Font::create(renderer, *face, {16 /* px */, TextureFilter::Linear});
drawText(renderer, **font, "Café crème", {16, 10}, {Color::white(), TextAlign::Left});
```

* A `Font` is one face at one pixel size. Glyphs are rasterised on first use and cached by code
  point. Code points that map to the same glyph share one atlas entry.
* Glyph images go into **atlas pages**: 512×512 dynamic textures by default. The pages are
  filled by a shelf packer with one pixel of padding, so linear filtering does not bleed between
  neighbours. When the current page is full, a new page is added, up to `maxPages` (4). Glyphs
  never move, so the atlas is never rebuilt and texture coordinates stay valid. Adding a glyph
  uploads only its rectangle (`updateTexture`).
* Pages hold premultiplied white with the coverage as alpha, so the text colour is the sprite
  tint and text batches with all other sprites (one atlas page is one texture slot).
* By default printable ASCII and Latin-1 are rasterised when the font is created, so typical
  Western European text adds nothing while a frame is drawn.
* **Missing glyphs.** A code point the face does not cover is drawn with the face's `.notdef`
  glyph (glyph 0), or `?` if `.notdef` is empty, and is listed in `missingCodepoints()`. A glyph
  that no longer fits into the atlas is drawn as nothing but still advances the pen
  (`failedGlyphCount()`).
* **Lifetime.** A `Font` owns its atlas textures and shares the `FontFace`; it must be destroyed
  before the graphics device.

## Layout (`TextLayout`)

* `drawText(renderer, glyphs, text, position, style)` draws UTF-8 text through the sprite batch.
  `position` is the top of the first line; `style` sets colour (straight alpha), alignment, layer
  and line spacing.
* `'\n'` starts a new line. Each line is aligned on its own: `Left`, `Center` or `Right` relative
  to `position.x`.
* Kerning is applied between neighbouring glyphs.
* Glyph positions are rounded to whole view units, so with the identity view glyphs map 1:1 onto
  target pixels.
* `measureText` returns the block size: the widest line, and the number of lines times the
  rounded line advance. `measureLine` measures one line. The demo sizes its help panel this way.
* No automatic word wrapping, justification, shaping or bidirectional text.
* Text is drawn in whatever view is current. In world space it moves and scales with the camera
  (the demo's shop signs); in screen space it stays fixed (the UI). Because glyphs are rasterised
  at one pixel size, text magnified by a camera zoom or by the window scale is resampled. For
  sharp text at another size, create a `Font` at that size.
