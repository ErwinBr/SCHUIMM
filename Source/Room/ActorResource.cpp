#include "Room/ActorResource.h"

#include "Core/Common/StringBuilder.h"

namespace schuimm::room {

Result<ResolvedActorClip> ActorResource::resolve(std::string_view animation, Facing facing) const
{
    const auto found = animations.find(animation);
    if (found == animations.end()) {
        return makeError(ErrorCode::NotFound, concat("actor '", id, "' has no animation '", animation, "'"));
    }
    const ActorAnimation& entry = found->second;
    const ActorClipMapping* mapping = nullptr;
    if (entry.directions[facingIndex(facing)]) {
        mapping = &*entry.directions[facingIndex(facing)];
    } else if (entry.any) {
        mapping = &*entry.any;
    } else if (entry.fallback && entry.directions[facingIndex(*entry.fallback)]) {
        mapping = &*entry.directions[facingIndex(*entry.fallback)];
    }
    if (mapping == nullptr) {
        return makeError(ErrorCode::NotFound, concat("actor '", id, "' animation '", animation, "' has no clip for facing '",
                                                     facingName(facing), "' and no fallback"));
    }
    return ResolvedActorClip{mapping->clip, mapping->flipX};
}

} // namespace schuimm::room
