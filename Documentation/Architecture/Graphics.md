# Graphics

## Layers and libraries

| Library | Contents | Depends on |
|---|---|---|
| `schuimm_graphics` (`Source/Graphics/Interface`, `Source/Graphics/Common`) | `GraphicsDevice`, `Renderer2D`, `Texture`, `RenderTarget`, `Sprite`, presentation maths; `Camera2D`, `SpriteBatcher`, `ShelfPacker`, `TextLayout`. No graphics API. | Core |
| `schuimm_graphics_opengl` (`Source/Graphics/OpenGL`) | The OpenGL 3.3 core backend: device, batched renderer, textures, render targets. | graphics, platform interface |
| `schuimm_graphics_resources` (`Source/GraphicsResources`) | `TextureCache` (image assets → textures), `Font` (font faces → glyph atlases). Uses only `Renderer2D`. | graphics, resource |
| `schuimm_room_graphics` (`Source/RoomGraphics`) | `RoomTextures` (a room's actor images → textures through `TextureCache`), `drawRoom` (a room's draw list → sprites). See [Actors.md](Actors.md#rendering). | room, graphics resources |

The generic resource layer never depends on graphics, and the renderer never depends on the
resource layer: `schuimm_graphics_resources` is the only bridge. Public interfaces expose no
OpenGL types or object names.

## Coordinate systems

| System | Unit, origin | Used for |
|---|---|---|
| **World** | game units (1 unit = 1 target pixel at zoom 1), any origin, y down | positions of scene objects |
| **Camera / view** | world relative to the camera's centre, scaled by zoom | inside `Camera2D` only |
| **Screen / target** | pixels of the current render target, origin top-left, y down | UI, `Renderer2D` with the identity view |
| **Virtual resolution** | the scene target (`PresentationSettings::virtualResolution`, 640×360 by default) | the size of "the screen" for game code |
| **Window** | points (logical pixels), origin top-left | mouse positions |
| **Framebuffer** | physical pixels of the window (Retina/HiDPI) | final composite only |

Each draw is in **view space**. The view transform (`Renderer2D::setView`) maps it to target
pixels: `camera.viewTransform()` for world rendering, the identity (`resetView()`) for UI.
Mouse input goes window → virtual resolution with `windowToVirtual()`, then virtual → world with
`camera.screenToWorld()`.

The virtual resolution never changes with the window. Resizing, fullscreen and DPI only change
the final composite, where the scene target is scaled into the window with letterboxing and,
optionally, integer scaling. Camera viewports and render targets are therefore independent of
the physical window size.

## Textures

* **Creation.** `Renderer2D::createTexture(TextureDescription)` or the convenience overload
  `createTexture(const Image&, TextureSettings, TextureUsage)`, which honours the image's row
  stride. Only `PixelFormat::RGBA8` exists. Errors (`InvalidArgument`) are reported for an
  empty size, a size above `GraphicsCapabilities::maxTextureSize`, a row stride smaller than a
  row, too little data, or a static texture without data.
* **Validation.** Renderer backends check caller-supplied layouts with the shared,
  overflow-safe `validatePixelLayout(size, byteCount, rowStride)` and
  `validateRegion(size, region)` (`Graphics/Interface/Texture.h`) before reading any pixels.
  The required byte count `stride * (height - 1) + width * 4` is computed with `checkedMul` /
  `checkedAdd` (`Core/Common/CheckedMath.h`), so a huge stride is reported as `InvalidArgument`
  ("row stride too large") instead of wrapping and passing a too-small buffer. Regions are
  checked with subtraction only (`width <= size.width - x`), so `INT_MAX` sizes cannot overflow.
  After validation, `stride * y` for every row is known to stay inside the data. The test
  `FakeRenderer` uses the same validators.
* **Sampling per texture.** `TextureSettings{filter, wrapU, wrapV}`: `Nearest` (pixel art) or
  `Linear` (smooth scaling); `ClampToEdge`, `Repeat` or `MirroredRepeat`. There is no global
  filtering mode. Textures have no mipmaps.
* **Premultiplied alpha on the GPU.** `Image` and `TextureDescription` data use straight alpha
  by default (`AlphaMode::Straight`). The upload multiplies colour by alpha once, so every GPU
  texture, uploaded or rendered, is premultiplied. All blending is then a single
  `glBlendFunc(ONE, ONE_MINUS_SRC_ALPHA)`, render-target contents composite correctly, and
  linear filtering does not bleed the colour of transparent texels into edges. Tint and vertex
  colours stay straight in the API and are premultiplied by the batcher. (Premultiplication
  happens on sRGB-encoded values, as is usual for 2D engines without linear-light blending.)
* **Mutability.** `TextureUsage::Static` textures are immutable. `Dynamic` textures accept
  `Renderer2D::updateTexture(texture, region, pixels)`, which flushes pending draws first so
  earlier draws see the old contents. The glyph atlas is the only user today.
* **Orientation.** Texture coordinate v = 0 is the top row, for uploaded images (row 0 is uploaded
  first) and for render targets (off-screen projections do not flip y).
* **Ownership.** `createTexture` returns `std::unique_ptr<Texture>`; `TextureCache` hands out
  `std::shared_ptr<const Texture>`. Every texture must be destroyed before the `GraphicsDevice`.
  The renderer counts live textures and render targets (`GraphicsDevice::liveResources()`) and
  verifies at destruction that none are left. A texture must also stay alive until the draws
  that use it are flushed (at the latest at the end of the frame).

### Texture cache

`TextureCache` turns image assets into textures through the `ResourceCache<Image>`:

```cpp
ResourceCache<Image> images(locator, loadImage);
TextureCache textures(device.renderer(), images);
auto hero = textures.load(ResourceId<Image>::parse("Images/player.png").value(), {TextureFilter::Nearest});
```

* Key: (image resource, `TextureSettings`). The same image with the same settings is one
  texture; with other settings it is a second texture made from the same decoded image.
* Errors keep the image cache's code (`NotFound`, `InvalidData`) or the renderer's, and name
  the asset.
* `unload(id, settings)`, `unload(id)` and `unloadAll()` drop the cache's references; holders keep
  their texture alive, exactly like `ResourceCache`.
* The decoded image stays in the image cache; `images.unloadAll()` after loading frees the CPU
  copies (the demo does this).

## Camera (`Camera2D`)

A backend-independent description of the visible part of the world. The renderer consumes its
transform and does not own a camera, so a game can use several (the demo has a main camera and a
minimap camera).

* `position` (world point at the centre of the viewport), `zoom` (clamped to configurable limits),
  `viewport` (size of the target it draws into, normally the virtual resolution).
* `viewTransform()` maps world → screen: `screen = (world − position) · zoom + viewport / 2`.
  `inverseViewTransform()`, `worldToScreen()`, `screenToWorld()` and `visibleWorldRect()` are
  derived from it.
* Optional `bounds`: the visible area is kept inside them; if they are smaller than the view
  along an axis, the camera centres on them.
* `follow(target, dt, smoothing)`: exponential smoothing where the remaining distance shrinks by
  `exp(−smoothing · dt)`, so the motion is the same at any frame rate. `smoothing ≤ 0` snaps.
  An optional dead zone ignores small offsets.
* `setPixelSnap(true)` rounds the screen translation to whole pixels, which stops nearest-
  filtered pixel art from shimmering while the camera moves.
* The camera does not rotate.

## Sprite batching

Application code submits `Sprite`s (or `drawTexture`, `fillRect`, `drawTriangle`, text) to
`Renderer2D`. A sprite has a texture (or none: a solid quad), a destination rectangle, an
optional source rectangle in texels, an origin (pivot), a rotation (radians, clockwise on
screen), a straight-alpha tint, horizontal/vertical flips and a `layer`.

**CPU side (`SpriteBatcher`, backend-independent, unit-tested).**
1. Each submission is turned into a quad immediately: corners are rotated around the pivot,
   transformed by the current view into target pixels, and get texture coordinates and the
   premultiplied RGBA8 tint. Triangles are degenerate quads.
2. At a flush the pending quads are ordered by `(layer, submission index)`. This is a stable
   order, made with `std::sort` and the index as tie-breaker, so it needs no temporary buffer. The
   sort is skipped entirely when layers were submitted in non-decreasing order (the common case).
3. Consecutive quads in that order are grouped into batches. A batch ends when it has no free
   texture slot for a new texture, or when it holds `maxQuadsPerBatch` (4096) quads.

**Ordering guarantee.** Nothing but the layer key changes the order. Sprites on the same layer
are drawn exactly in submission order, so overlapping transparent artwork is never reordered for
batching efficiency. Games get strict back-to-front control through `layer` (the demo uses the
feet y coordinate, which gives y-sorted scenes).

**Texture slots.** A batch can use several textures. The device queries
`GL_MAX_TEXTURE_IMAGE_UNITS` and uses `min(units, 16)` slots (OpenGL 3.3 guarantees 16;
`GraphicsCapabilities::textureSlots`). Each vertex carries its slot index. GLSL 3.30 only allows
constant indices into sampler arrays, so the fragment shader selects the sampler with a branch
chain on the flat slot value. This is well defined because the slot is constant per primitive and
textures have no mipmaps. When a batch needs one texture more than there are slots, a new batch
(draw call) starts. With a single slot the renderer still works, with one draw call per texture
change. Unused slots are bound to a 1×1 white texture, which also serves solid quads and
triangles, so they batch together with textured sprites.

**Flushes.** Pending draws are flushed by `flush()`, `clear()`, `setView()`/`resetView()`,
`beginRenderTarget()`/`endRenderTarget()`, `updateTexture()`, the end of the frame, and when the
pending-quad capacity (65536) is reached. Layer ordering applies to the draws between two
flushes. For example, UI drawn after `resetView()` is always on top of the world drawn before.

**GPU side.** One VAO, one streamed vertex buffer sized for a full batch (orphaned with
`glBufferData(nullptr)` before every batch), and one static `uint16` index buffer with the two
triangles of every quad. Each batch binds its textures to units 0..N−1 and issues one
`glDrawElements`. Vertices are 24 bytes: position, texture coordinates, RGBA8 colour, slot.

**Allocation.** The batcher's buffers grow to their high-water mark and are reused (`clear()`
keeps their capacity); steady-state frames do not allocate (checked by a unit test).

**Statistics.** `Renderer2D::lastFrameStatistics()` reports the previous frame's quads, draw
calls, flushes and render-target switches. The demo shows them on screen.

## Render targets

```cpp
auto target = renderer.createRenderTarget({{320, 180}, {TextureFilter::Nearest}});
renderer.beginRenderTarget(**target);
renderer.clear({0, 0, 0, 0});
// ... draw ...
renderer.endRenderTarget();
renderer.drawTexture((*target)->texture(), {0, 0, 640, 360});
```

* A `RenderTarget` is an off-screen RGBA8 image with its own size, independent of window and
  virtual resolution; `texture()` is drawn like any texture. Its contents are premultiplied and
  start transparent.
* `GLRenderTarget` owns the framebuffer object and its texture (RAII). Creation checks the size
  (`InvalidArgument` for ≤ 0 or above `maxTextureSize`) and framebuffer completeness
  (`GraphicsFailure` with the status code).
* `beginRenderTarget` / `endRenderTarget` nest (a stack). Each switch flushes pending draws and
  then explicitly establishes the framebuffer binding, viewport, projection and blend state.
  Nothing relies on state left over from earlier code. `beginRenderTarget` resets the view to
  the identity (target pixels); `endRenderTarget` restores the parent target and its view.
  Unbalanced calls are checked at the end of the frame. Drawing a target's own texture into it
  is caught by an assertion.
* The scene itself is a render target at the virtual resolution owned by the device; it is the
  bottom of the stack during a frame.

## Device and frame flow (OpenGL)

**Function loading.** `GLFunctions.h` declares the GL types, the constants used and an X-macro
list of the OpenGL 3.3 entry points the renderer calls. `loadFunctions()` resolves them through
`OpenGLSurface::getProcAddress` and fails with the name of the first missing one. The table is a
device member, not a global; no Khronos headers are vendored.

**Context validation.** The device logs version, renderer, vendor, GLSL version, max texture size
and texture units, and fails unless the context is a core profile of version 3.3 or newer.

**Frame.**
1. `beginFrame()` makes the scene target the current target (framebuffer, viewport, projection,
   premultiplied blending) and resets the statistics.
2. The application draws through `Renderer2D` (and may use further render targets).
3. `endFrame()` flushes the scene, checks the target stack, then switches to the window's default
   framebuffer (blending off), clears it to the border colour and draws the scene texture into
   the letterboxed rectangle. A requested capture reads the back buffer into a top-down `Image`.
   GL errors are logged (at most 10 times).
4. `present()` swaps buffers.

Everything stays within OpenGL 3.3 core, and therefore also within macOS's 4.1 core profile.
The renderer uses no fixed-function pipeline, no deprecated features and no extensions. GLSL is
`330 core`.

## Text

Font loading, glyph atlases and text layout are described in [Text.md](Text.md). Text is drawn
as sprites through the same batch.

## Replacing the renderer (Direct3D, Metal, Vulkan)

Application code depends only on `GraphicsDevice`, `Renderer2D`, `Texture` and `RenderTarget`.
`SpriteBatcher`, `Camera2D`, `TextLayout`, `TextureCache` and `Font` are backend-independent and
would be reused as they are. A new backend:

1. Adds a surface type to the platform layer (`createMetalLayer()`, `createVulkanSurface(...)`, a
   D3D swap chain target), each returning an opaque, backend-specific surface object.
2. Implements `GraphicsDevice`, `Renderer2D`, `Texture` and `RenderTarget` in
   `Source/Graphics/<API>/`: uploads `SpriteBatcher` output and draws its batches, and offers a
   `create<API>GraphicsDevice(...)` factory.
3. Is chosen in the composition root (`main.cpp`).

Shaders would be written per backend or produced from a common source by an offline tool.
