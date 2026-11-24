# Bridge between the resource layer (CPU-side assets) and the graphics layer (GPU resources):
# texture cache and, later, fonts with glyph atlases. It uses only the Renderer2D interface, so
# it is independent of the graphics backend. Neither the resource layer nor the renderer
# depends on it.
add_library(schuimm_graphics_resources STATIC
    Font.cpp
    TextureCache.cpp)
add_library(Schuimm::GraphicsResources ALIAS schuimm_graphics_resources)
target_link_libraries(schuimm_graphics_resources PUBLIC Schuimm::Graphics Schuimm::Resource)
schuimm_configure_target(schuimm_graphics_resources)
