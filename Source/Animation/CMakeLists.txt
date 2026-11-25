# Frame-based 2D animation: immutable animation resources (*.anim.json), playback state and the
# conversion from engine time to animation ticks. Independent of actors, rooms, scripts and the
# GPU. JSON decoding (Schuimm::ResourceJson) is private.
add_library(schuimm_animation STATIC
    AnimationClock.cpp
    AnimationPlayer.cpp
    AnimationResource.cpp
    AnimationResourceLoader.cpp)
add_library(Schuimm::Animation ALIAS schuimm_animation)
target_link_libraries(schuimm_animation
    PUBLIC Schuimm::Resource
    PRIVATE Schuimm::ResourceJson)
schuimm_configure_target(schuimm_animation)
