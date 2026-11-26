#pragma once

// What it takes to draw an actor's current frame, computed from runtime state without any
// graphics API: the image, the source rectangle, and where the frame lands in room coordinates.
//
// Placement. An actor's position is its ground point (normally its feet). The frame's pivot, in
// pixels from the source rectangle's top-left corner on pixel edges, is put on that point:
//
//     px = flipX ? width - pivot.x : pivot.x        (mirroring mirrors the pivot as well)
//     destination = { position.x - px * scale, position.y - pivot.y * scale,
//                     width * scale, height * scale }
//
// so the ground point stays where it is whatever the frame's size and pivot, the mirroring or
// the scale. A sprite drawn into `destination` with its source rectangle (flipped when flipX)
// shows the frame (SpriteBatcher mirrors the texture coordinates inside the rectangle).

#include "Animation/AnimationResource.h"
#include "Core/Common/Geometry.h"
#include "Resource/AssetPath.h"


namespace schuimm::room {

struct ActorRenderState {
    AssetPath image;
    RectI source;
    Vec2 position; // Ground point, room coordinates.
    Vec2 pivot;    // As in the frame (not mirrored).
    float scale = 1.0f;
    bool flipX = false;
    RectF destination; // Room coordinates.
};

[[nodiscard]] ActorRenderState makeActorRenderState(const animation::AnimationFrame& frame, Vec2 position, float scale,
                                                    bool flipX);

} // namespace schuimm::room
