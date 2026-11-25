# Drawing rooms: GPU textures for a room's actors (through the TextureCache) and sprite
# submission of a room's draw list through the Renderer2D interface. The room library itself
# stays free of graphics; this bridge is the only place where the two meet.
add_library(schuimm_room_graphics STATIC
    RoomRenderer.cpp
    RoomTextures.cpp)
add_library(Schuimm::RoomGraphics ALIAS schuimm_room_graphics)
target_link_libraries(schuimm_room_graphics PUBLIC Schuimm::Room Schuimm::GraphicsResources)
schuimm_configure_target(schuimm_room_graphics)
