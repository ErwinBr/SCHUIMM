#include "Animation/AnimationResource.h"

#include <algorithm>

namespace schuimm::animation {

std::size_t AnimationClip::frameAt(std::uint64_t elapsed) const noexcept
{
    // frameStarts[0] == 0, so upper_bound never returns begin().
    const auto next = std::upper_bound(frameStarts.begin(), frameStarts.end(), elapsed);
    return static_cast<std::size_t>(next - frameStarts.begin()) - 1;
}

std::optional<std::size_t> AnimationResource::findClip(std::string_view name) const noexcept
{
    const auto found = std::lower_bound(clips.begin(), clips.end(), name,
                                        [](const AnimationClip& clip, std::string_view n) { return clip.name < n; });
    if (found == clips.end() || found->name != name) return std::nullopt;
    return static_cast<std::size_t>(found - clips.begin());
}

std::optional<AnimationClipRef> AnimationClipRef::find(std::shared_ptr<const AnimationResource> resource,
                                                       std::string_view name)
{
    if (!resource) return std::nullopt;
    const auto index = resource->findClip(name);
    if (!index) return std::nullopt;
    return AnimationClipRef(std::move(resource), *index);
}

} // namespace schuimm::animation
