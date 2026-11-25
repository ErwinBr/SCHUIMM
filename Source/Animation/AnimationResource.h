#pragma once

// Immutable, reusable frame animations: an animation resource (*.anim.json) holds named clips,
// each an ordered sequence of frames. Independent of actors, rooms, scripts and the GPU: a frame
// names its image by asset path, and loading a resource needs no graphics context.
//
// Every frame has a concrete source rectangle (in image pixels, origin top-left): a frame that
// gives none in its file covers its whole image. The pivot is in pixels relative to the source
// rectangle's top-left corner, on pixel edges: [0, 0] is the top-left corner of the frame,
// [width, height] its bottom-right corner. It may lie outside the frame.
//
// Durations are whole animation ticks (AnimationClock.h: animationTicksPerSecond).

#include "Core/Common/Geometry.h"
#include "Resource/AssetPath.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::animation {

inline constexpr int animationResourceVersion = 1;
inline constexpr std::size_t maxFramesPerClip = 1024;
inline constexpr std::uint32_t maxFrameDuration = 0x7FFFFFFFu;
// The sum of a clip's frame durations; keeps playback arithmetic far from overflow.
inline constexpr std::uint64_t maxClipDuration = 0xFFFFFFFFu;

struct AnimationFrame {
    AssetPath image;
    RectI source;
    Vec2 pivot;
    std::uint32_t duration = 1; // Ticks, at least 1.

    friend bool operator==(const AnimationFrame&, const AnimationFrame&) = default;
};

struct AnimationClip {
    std::string name;
    bool loop = false;
    std::vector<AnimationFrame> frames; // At least one.
    std::uint64_t totalDuration = 0;    // Sum of the frame durations.
    std::vector<std::uint64_t> frameStarts; // frameStarts[i]: tick at which frame i begins.

    // The frame shown `elapsed` ticks into the clip (the last frame from totalDuration on).
    [[nodiscard]] std::size_t frameAt(std::uint64_t elapsed) const noexcept;

    friend bool operator==(const AnimationClip&, const AnimationClip&) = default;
};

struct AnimationResource {
    std::vector<AnimationClip> clips; // Sorted by name; names are unique.

    [[nodiscard]] std::optional<std::size_t> findClip(std::string_view name) const noexcept;

    friend bool operator==(const AnimationResource&, const AnimationResource&) = default;
};

// A clip of a shared resource. Holding the resource keeps the clip alive however long it is
// played, even if the resource is unloaded from its cache meanwhile.
class AnimationClipRef {
public:
    AnimationClipRef() = default;
    // nullopt if `resource` is null or has no clip `name`.
    [[nodiscard]] static std::optional<AnimationClipRef> find(std::shared_ptr<const AnimationResource> resource,
                                                              std::string_view name);

    [[nodiscard]] bool isValid() const noexcept { return resource_ != nullptr; }
    // Requires isValid().
    [[nodiscard]] const AnimationClip& clip() const noexcept { return resource_->clips[index_]; }
    [[nodiscard]] const std::shared_ptr<const AnimationResource>& resource() const noexcept { return resource_; }
    [[nodiscard]] std::size_t index() const noexcept { return index_; }

    // The same clip of the same resource object.
    friend bool operator==(const AnimationClipRef& a, const AnimationClipRef& b) noexcept
    {
        return a.resource_ == b.resource_ && a.index_ == b.index_;
    }

private:
    AnimationClipRef(std::shared_ptr<const AnimationResource> resource, std::size_t index)
        : resource_(std::move(resource))
        , index_(index)
    {
    }

    std::shared_ptr<const AnimationResource> resource_;
    std::size_t index_ = 0;
};

} // namespace schuimm::animation
