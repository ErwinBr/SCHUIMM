# Graphics

## Interfaces (`Source/Graphics/Interface`)

* `GraphicsDevice` owns the context and the frame protocol: `beginFrame()`, `renderer()`,
  `endFrame()`, `present()`, plus `setVSync()`, `capabilities()` and frame capture for tests.
* `Renderer2D` is the backend-neutral drawing API used by application code: `clear`,
  `drawTriangle` (per-vertex colours), `drawTexture` (destination rect, normalised source rect,
  tint), `createTexture` (RGBA8 pixels → `unique_ptr<Texture>`). Coordinates are
  virtual-resolution pixels with the origin at the top-left. Every draw is alpha-blended.
* `PresentationSettings` defines the virtual resolution (default 640×360), the scaling filter,
  optional integer scaling and the border colour. `computePresentationRect` and `windowToVirtual`
  are pure functions (unit-tested) shared by the renderer and by input mapping.

## OpenGL foundation (`Source/Graphics/OpenGL`)

**Function loading.** `GLFunctions.h` declares the GL types, the constants used, and an X-macro
list of the ~55 OpenGL 3.3 entry points the renderer calls. `loadFunctions()` resolves each one
through `OpenGLSurface::getProcAddress` and fails with the name of the first missing function.
The table is a member of the device, not a global. No Khronos headers are vendored; the
values follow the official registry (`gl.xml`).

**Context validation.** After loading, the device reads `GL_VERSION`, `GL_RENDERER`, `GL_VENDOR`,
`GL_SHADING_LANGUAGE_VERSION`, `GL_MAJOR/MINOR_VERSION`, `GL_CONTEXT_PROFILE_MASK` and
`GL_MAX_TEXTURE_SIZE`, logs them, and fails unless the context is a core profile of version 3.3
or newer.

**Pipeline.**

* Two GLSL `330 core` programs: colour (position + colour) and textured (position + UV + tint,
  `texture(uTexture, uv) * colour`). A `uTargetSize` uniform converts pixel coordinates to clip
  space, flipping y so the origin is at the top-left.
* Triangles: one VAO with a streamed VBO (orphaned with `glBufferData(nullptr)` each draw).
* Quads: one VAO with a streamed VBO of four vertices and a static **EBO** with six `uint16`
  indices recorded in the VAO.
* Textures: `GL_RGBA8`, `CLAMP_TO_EDGE`, nearest or linear filtering, `UNPACK_ALIGNMENT = 1`.
  Image row 0 is uploaded at t = 0, so v = 0 is the top row of the source image.
* Blending: `glBlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE_MINUS_SRC_ALPHA)`
  (straight alpha for colour, "over" accumulation for the target's alpha).

**Frame flow.**

1. `beginFrame()` binds the scene **framebuffer object** (an RGBA8 texture at the virtual
   resolution), sets the viewport and enables blending.
2. The application draws through `Renderer2D`.
3. `endFrame()` binds the default framebuffer, clears it to the border colour and draws the scene
   texture into the letterboxed rectangle (v flipped, blending off). If a capture was requested, the
   back buffer is read with `glReadPixels` and flipped into a top-down `Image` (the CPU-side
   RGBA8 type from `Core/Image`, shared with the resource layer). GL errors are
   logged (at most 10 times).
4. `present()` swaps buffers through the surface.

Rendering at a fixed virtual resolution and scaling at the end is the standard approach for 2D
adventure games: game logic and assets work in one coordinate space, and window size, Retina or
DPI scaling, and fullscreen only affect the final composite.

Nothing deprecated is used: no fixed-function pipeline, no immediate mode, no client-side vertex
arrays, no compatibility profile. Everything stays within OpenGL 3.3 core and therefore also
within macOS's OpenGL 4.1.

## Replacing the renderer (Direct3D, Metal, Vulkan)

Application and future adventure-framework code depend only on `GraphicsDevice`, `Renderer2D` and
`Texture`. A new backend:

1. Adds a surface type to the platform layer for the new API. `Window::createOpenGLSurface` has
   siblings such as `createMetalLayer()` (a `CAMetalLayer` on the content view),
   `createVulkanSurface(VkInstance)` (`vkCreateWin32/Xlib/WaylandSurfaceKHR`) or a D3D swap
   chain target (`HWND`), each returning an opaque, backend-specific surface object. Only the
   platform backend implements them.
2. Implements `GraphicsDevice` and `Renderer2D` in `Source/Graphics/<API>/` (a `Texture`
   subclass, the frame protocol, the virtual-resolution target and letterbox composite), and
   offers a `create<API>GraphicsDevice(surface, settings, logger)` factory.
3. Chooses the backend in the composition root (`main.cpp`), or behind a small factory if
   several backends are shipped together.

Nothing above the `GraphicsDevice` interface changes. Shaders would be written per backend
(GLSL, MSL, HLSL/SPIR-V) or produced from a common source by an offline tool in a later milestone.
The interface deliberately avoids exposing API concepts (no handles, no shader objects, no
state).
