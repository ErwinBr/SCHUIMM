#include "Room/ActorRenderState.h"

namespace schuimm::room {

ActorRenderState makeActorRenderState(const animation::AnimationFrame& frame, Vec2 position, float scale, bool flipX)
{
    const auto width = static_cast<float>(frame.source.width);
    const auto height = static_cast<float>(frame.source.height);
    const float pivotX = flipX ? width - frame.pivot.x : frame.pivot.x;
    return ActorRenderState{
        frame.image,
        frame.source,
        position,
        frame.pivot,
        scale,
        flipX,
        RectF{position.x - pivotX * scale, position.y - frame.pivot.y * scale, width * scale, height * scale},
    };
}

} // namespace schuimm::room
