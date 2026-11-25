#include "Animation/AnimationPlayer.h"

namespace schuimm::animation {

const char* playbackStatusName(PlaybackStatus status) noexcept
{
    switch (status) {
    case PlaybackStatus::Stopped: return "stopped";
    case PlaybackStatus::Playing: return "playing";
    case PlaybackStatus::Paused: return "paused";
    case PlaybackStatus::Finished: return "finished";
    }
    return "unknown";
}

bool AnimationPlayer::play(AnimationClipRef clip)
{
    if (!clip.isValid()) {
        clip_ = AnimationClipRef();
        elapsed_ = 0;
        status_ = PlaybackStatus::Stopped;
        return false;
    }
    if (clip == clip_ && (status_ == PlaybackStatus::Playing || status_ == PlaybackStatus::Paused)) return false;
    clip_ = std::move(clip);
    elapsed_ = 0;
    status_ = PlaybackStatus::Playing;
    return true;
}

void AnimationPlayer::restart() noexcept
{
    if (!clip_.isValid()) return;
    elapsed_ = 0;
    status_ = PlaybackStatus::Playing;
}

void AnimationPlayer::stop() noexcept
{
    elapsed_ = 0;
    status_ = PlaybackStatus::Stopped;
}

void AnimationPlayer::pause() noexcept
{
    if (status_ == PlaybackStatus::Playing) status_ = PlaybackStatus::Paused;
}

void AnimationPlayer::resume() noexcept
{
    if (status_ == PlaybackStatus::Paused) status_ = PlaybackStatus::Playing;
}

AdvanceResult AnimationPlayer::advance(std::uint64_t ticks) noexcept
{
    if (status_ != PlaybackStatus::Playing || ticks == 0) return {};
    const AnimationClip& clip = clip_.clip();
    const std::uint64_t total = clip.totalDuration; // In [1, maxClipDuration].
    if (clip.loop) {
        // elapsed_ < total <= 2^32 - 1 and ticks % total < total: the sum cannot overflow.
        elapsed_ = (elapsed_ + ticks % total) % total;
        return {};
    }
    if (ticks >= total - elapsed_) {
        elapsed_ = total;
        status_ = PlaybackStatus::Finished;
        return {true};
    }
    elapsed_ += ticks;
    return {};
}

} // namespace schuimm::animation
