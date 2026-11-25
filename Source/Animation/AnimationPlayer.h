#pragma once

// Playback of one clip at a time: which clip, how far into it, and whether it is playing. The
// clip's resource is shared (AnimationClipRef), so playback never refers to a destroyed
// resource, and every player keeps its own state: any number of players can play the same clip.
//
// Status:
//   Stopped   at the clip's first frame, not advancing (also the state of a player without clip)
//   Playing   advancing with advance()
//   Paused    advancing resumes where it was
//   Finished  a non-looping clip reached its end; it stays on its last frame
//
// play(clip) never restarts a clip that is already playing or paused: movement code may request
// the same clip every frame. Requesting the current clip when it is stopped or finished starts it
// again from the beginning. restart() always starts from the beginning.

#include "Animation/AnimationResource.h"

#include <cstddef>
#include <cstdint>

namespace schuimm::animation {

enum class PlaybackStatus {
    Stopped,
    Playing,
    Paused,
    Finished,
};

[[nodiscard]] const char* playbackStatusName(PlaybackStatus status) noexcept;

struct AdvanceResult {
    // True only for the advance() in which a non-looping clip reached its end.
    bool finished = false;
};

class AnimationPlayer {
public:
    // Selects `clip` and plays it. Returns true if playback (re)started from the beginning,
    // false if `clip` was already selected and playing or paused (nothing changes). An invalid
    // clip reference stops the player and clears its clip.
    bool play(AnimationClipRef clip);
    // Starts the current clip from the beginning (no effect without a clip).
    void restart() noexcept;
    // Back to the first frame, not advancing.
    void stop() noexcept;
    void pause() noexcept;
    void resume() noexcept;

    // Advances a playing clip by `ticks`. Takes constant time however large `ticks` is (whole
    // loops are skipped arithmetically) apart from locating the frame (O(log frames)).
    AdvanceResult advance(std::uint64_t ticks) noexcept;

    [[nodiscard]] bool hasClip() const noexcept { return clip_.isValid(); }
    [[nodiscard]] const AnimationClipRef& clip() const noexcept { return clip_; }
    [[nodiscard]] PlaybackStatus status() const noexcept { return status_; }
    // Ticks into the current clip: [0, totalDuration) while looping, up to totalDuration for a
    // finished clip.
    [[nodiscard]] std::uint64_t elapsedTicks() const noexcept { return elapsed_; }
    // Requires hasClip().
    [[nodiscard]] std::size_t frameIndex() const noexcept { return clip_.clip().frameAt(elapsed_); }
    [[nodiscard]] const AnimationFrame& currentFrame() const noexcept { return clip_.clip().frames[frameIndex()]; }

private:
    AnimationClipRef clip_;
    std::uint64_t elapsed_ = 0;
    PlaybackStatus status_ = PlaybackStatus::Stopped;
};

} // namespace schuimm::animation
