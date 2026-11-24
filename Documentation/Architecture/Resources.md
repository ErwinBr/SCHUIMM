# Resources and assets

The resource layer (`Source/Resource`, target `schuimm_resource`) finds asset files, identifies
resources, loads them into CPU memory and decodes images. It is deliberately small: a foundation
for the texture, font and audio systems of Milestone 2, not an asset pipeline.

## Four kinds of data, kept apart

| | What | Type | Owner | Library |
|---|---|---|---|---|
| 1 | Asset data on disk | file below the asset root, named by an `AssetPath` | the file system | `AssetLocator` (`schuimm_resource`) reads it via `schuimm_filesystem` |
| 2 | CPU-side decoded data | `Image` (`Core/Image/Image.h`) | whoever holds the value | `decodeImage()` produces it (`schuimm_resource`) |
| 3 | Runtime resource objects | `std::shared_ptr<const T>` from a `ResourceCache<T>` | the cache plus every holder | `schuimm_resource` |
| 4 | GPU-side resources | `Texture` (`std::unique_ptr`), created by `Renderer2D` | the application, destroyed before the `GraphicsDevice` | `schuimm_graphics_*` |

Generic resource code never calls the graphics API: loading and decoding run without a window
or OpenGL context (the unit tests do exactly that). Turning an `Image` into a `Texture` is a
separate, explicit step in the graphics layer; the GPU texture cache belongs to Milestone 2.

```text
 disk ──AssetLocator::readBytes──► bytes ──decodeImage──► Image ──ResourceCache<Image>──► shared_ptr<const Image>
                                                                         │ (Milestone 2, graphics layer)
                                                                         ▼
                                                    Renderer2D::createTexture ──► unique_ptr<Texture>
```

## Dependencies

```text
schuimm_core (Common, Image, ...)  ◄── schuimm_filesystem (Path, FileSystem: POSIX | Win32)
                                          ▲
                                   schuimm_resource ──► libpng ──► zlib   (private, ThirdParty/)
```

`schuimm_resource` does not depend on the platform windowing backend, the graphics interface or
the application. `Image` lives in Core because both the resource layer (producer) and the
graphics layer (consumer, and producer for frame capture) use it without depending on each other.

## Asset paths (`AssetPath`)

An `AssetPath` names an asset relative to the asset root, the same way on every platform.
`AssetPath::parse()` enforces:

* UTF-8, `/` as the only separator. `\` and `:` are rejected (no Windows separators, drive
  letters, URLs or alternate data streams), as are control characters.
* Relative: a leading `/` is rejected.
* Normalised: `.`, repeated and trailing separators are removed; `a/../b` becomes `b`. A `..`
  that would leave the root (`../x`, `a/../../x`) is rejected, so a resolved asset path cannot
  point outside the asset root lexically. (Symbolic links inside the root are followed by the
  operating system and may point elsewhere.)
* Case-sensitive comparison. On case-insensitive file systems (macOS, Windows) `Hero.png` also
  opens `hero.png`; the engine does not detect such mismatches (see
  [KnownLimitations.md](KnownLimitations.md)).

## Asset root and path resolution (`AssetLocator`)

```cpp
auto exe = fs::executableDirectory();                         // never the working directory
auto locator = AssetLocator::create("Assets", *exe);          // relative root: resolved against exe
auto locator = AssetLocator::create("/home/me/game/Assets");  // absolute root (development)
auto locator = locateAssetRoot(defaultAssetRootCandidates(*fs::executablePath()));   // packaged game
```

* The root is fixed at creation, absolute and normalised. A relative root is resolved against an
  explicit base directory. If the base is not absolute, creation fails instead of falling back to
  the current working directory.
* Creation fails with `NotFound` if the root does not exist, `InvalidArgument` if it is not a
  directory.
* `resolve(asset)` joins the root and the asset path (pure string operation).
* `info`, `exists` and `readBytes` access the file. Errors keep the file system's code
  (`NotFound`, `PermissionDenied`, `IoFailure`) and name the asset path, the root and the OS
  reason, for example:
  `NotFound: asset 'Images/missing.png' not found (asset root '/game/Assets'): cannot open file '/game/Assets/Images/missing.png': No such file or directory`.
* `defaultAssetRootCandidates(executablePath)` is the convention for packaged games:
  `X.app/Contents/Resources/Assets` for a macOS bundle, otherwise `Assets` next to the
  executable. `locateAssetRoot()` returns the first that exists and otherwise lists every path it
  tried. For development projects the application passes an explicit root (for example from the
  command line or a project file).

The composition root (`main`) creates the locator and passes it on. There is no global asset
root. The EngineDemo has no image assets yet, so it creates no locator.

## Resource identification (`ResourceId<T>`)

`ResourceId<T>` wraps the normalised `AssetPath` and its precomputed 64-bit FNV-1a hash.

* **Type safety:** `ResourceId<Image>` and `ResourceId<Font>` are unrelated types, so an image
  identifier cannot be used where a font is expected, even for the same file (checked with
  `static_assert` in the tests).
* **Equality and hashing** use the normalised path, so `ui/../hero.png` and `hero.png` are the
  same resource. The hash is deterministic across platforms and runs.
* **No GUIDs or asset database.** The path is the identity until a concrete need appears, such as
  renaming assets without breaking references or packed archives.

## Loading, ownership and lifetime (`ResourceCache<T>`)

```cpp
ResourceCache<Image> images(locator, loadImage);   // locator must outlive the cache
auto hero = images.load(ResourceId<Image>::parse("sprites/hero.png").value());
if (!hero) { log(describe(hero.error())); }        // NotFound / InvalidData / ...
std::shared_ptr<const Image> image = *hero;
images.unload(id);                                 // cache lets go; `image` stays valid
```

* **Explicit and synchronous.** `load(id)` returns the cached resource or calls the loader.
  Nothing loads implicitly or in the background.
* **No duplicates.** A second `load` of the same identifier returns the same object without
  touching the disk.
* **Failures are returned, not cached.** A later `load` retries, so an asset added after a
  failure is picked up.
* **Shared, immutable ownership.** The cache owns each resource through
  `std::shared_ptr<const T>`. `unload(id)` and `unloadAll()` (and the cache's destructor) drop the
  cache's reference. A caller holding the pointer keeps the resource alive, and it is destroyed
  when the last holder releases it. Unloading can therefore never leave a dangling reference.
  The trade-off is that memory is only reclaimed once every holder has let go.
* **Loaders do CPU work only.** The loader has the signature
  `Result<T>(const AssetLocator&, const AssetPath&)`, for example `loadImage`, which reads bytes
  and decodes them. It must not touch the GPU.
* **One thread.** The cache is not thread-safe. There is no asynchronous loading yet.
* **No global state.** Caches are ordinary objects created by the application.

Adding a resource type (fonts, audio) means writing a CPU-side type, a loader, and a
`ResourceCache<NewType>`. Nothing else changes.

## Image decoding (`Resource/Image`)

```cpp
Result<Image> decodeImage(std::span<const std::byte> data, std::string_view sourceName);
Result<Image> loadImage(const AssetLocator&, const AssetPath&);  // readBytes + decodeImage
```

* The format is detected from its signature bytes, not from the file extension. PNG is the only
  supported format.
* `PngDecoder.cpp` is the only file that includes `png.h`. It uses libpng's simplified
  `png_image` API, which reports errors through return values instead of `setjmp`/`longjmp`, and
  requests `PNG_FORMAT_RGBA`.
* Every PNG colour type and bit depth is accepted:
  * palette and greyscale images are expanded;
  * `tRNS` becomes alpha;
  * 16-bit channels are reduced to 8 bits (a 16-bit PNG without colour-space information is
    treated as linear by libpng and converted to sRGB);
  * interlaced images are de-interlaced;
  * opaque images get alpha 255.
* Width and height are limited to `Image::maxDimension` (16384). Larger images, and corrupt,
  truncated or unrecognised data, fail with `InvalidData` and a message naming the source.

Adding a format means adding a signature to `detectImageFileFormat` and a decoder file next to
`PngDecoder.cpp`. Callers do not change.

## CPU-side image representation (`Core/Image/Image.h`)

| Property | Value |
|---|---|
| Pixel format | `PixelFormat::RGBA8`: bytes R, G, B, A, 8 bits each |
| Colour encoding | sRGB, as stored in the file |
| Alpha | straight (not premultiplied). A pixel with alpha 0 keeps its colour. |
| Layout | rows top to bottom; row `y` starts at `y * rowStride()`; created images are tightly packed (`rowStride() == width * 4`) |
| Ownership | value type: deep copy, cheap move, owns its `std::vector` of bytes |
| Access | `size()`, `width()`, `height()`, `format()`, `rowStride()`, `pixels()`, `row(y)`, `pixel(x, y)` |

`Renderer2D::createTexture` currently takes tightly packed RGBA8 bytes, which is exactly
`image.pixels()`. Consumers should still honour `rowStride()` so padded layouts can be added
without breaking them.

## Tests

`Tests/Resource` (label `unit`, headless) covers:
* asset-path rules;
* root configuration, including relative roots against an explicit base;
* path resolution;
* missing assets and their error text;
* the default root candidates;
* identifier type safety;
* cache deduplication, failure retry, unload and ownership (with an instance-counting probe type);
* image decoding of every PNG colour type, 16-bit and interlaced files, exact pixel and alpha
  values, and invalid files.

The PNG files are generated by `Tests/Assets/generate_test_images.py` (see
`Tests/Assets/README.md`). The file system layer has its own tests in
`Tests/Platform/FileSystemTests.cpp` and `PathTests.cpp`.
