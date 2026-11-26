# Animation: frame animations, playback and ticks

Frame-based 2D animation lives in `Source/Animation` (library `schuimm_animation`). It covers
three things:
- immutable animation resources (`*.anim.json`);
- playback state (`AnimationPlayer`);
- converting engine time to animation ticks (`AnimationClock`).

It does not depend on actors, rooms, scripts or the GPU. Actors use it ([Actors.md](Actors.md)),
and anything else that needs a frame animation can too.

## Three things, kept apart

| | What it is | Mutable | Shared |
|---|---|---|---|
| `AnimationResource` (`Animation/AnimationResource.h`) | named clips, each an ordered list of frames | no | by every player, through `shared_ptr` |
| `AnimationPlayer` (`Animation/AnimationPlayer.h`) | which clip, how far into it, and whether it is playing | yes | no: each player is independent |
| `AnimationClock` (`Animation/AnimationClock.h`) | turns seconds into whole ticks without losing fractions | yes | one per room instance |

A frame names its image by asset path, so loading a resource needs no graphics context. The
GPU textures are made separately ([Actors.md](Actors.md#rendering)).

## The animation resource format, version 1

```json
{
    "version": 1,
    "clips": {
        "idle_south": {
            "loop": true,
            "frames": [
                { "image": "Actors/Walker/walker.png", "source": [0, 8, 24, 40], "pivot": [12, 40], "duration": 30 },
                { "image": "Actors/Walker/walker.png", "source": [32, 6, 24, 42], "pivot": [12, 42], "duration": 20 }
            ]
        },
        "flicker": {
            "loop": true,
            "frames": [ { "image": "Actors/Lamp/lamp_on.png", "pivot": [8, 48], "duration": 50 } ]
        }
    }
}
```

The ActorDemo's resources are complete examples:
[`walker.anim.json`](../../Applications/ActorDemo/Assets/Actors/Walker/walker.anim.json) uses
source rectangles on a sprite sheet, and
[`lamp.anim.json`](../../Applications/ActorDemo/Assets/Actors/Lamp/lamp.anim.json) uses whole
images.

| Property | Type | Rules |
|---|---|---|
| `version` | integer, **required** | must be `1`; checked first ([ResourceFormats.md](ResourceFormats.md)) |
| `clips` | object, **required** | at least one clip; keys are clip names (identifiers), so they are unique |
| `clips.<name>.loop` | boolean, **required** | whether the clip loops |
| `clips.<name>.frames` | array, **required** | 1 to 1024 frames |
| `image` | asset path, **required** | the frame's image (PNG) |
| `source` | `[x, y, width, height]` | integers, in image pixels from the top-left corner. They must lie inside the image. If omitted, the frame is the whole image. |
| `pivot` | `[x, y]` | default `[0, 0]`. Finite, at most 2^20 from the origin. It may lie outside the frame. |
| `duration` | integer, **required** | ticks, from 1 to 2^31−1. A clip's total must be at most 2^32−1. |

**Validation rules:**
- Unknown properties, duplicate keys and other versions are errors, as in every engine format.
- Frames may differ in size.
- Errors are `InvalidData` with `"<file>: <property path>: <reason>"`, for example
  `Hero.anim.json: clips.idle.frames[1].source: rectangle [20, 0, 11, 40] is not inside the 30x40 image`.
  A missing image keeps its own code (`NotFound`).

**Image sizes** are read from the image headers (`readImageInfo` and `loadImageInfo` in
`Resource/Image`, through libpng's header reader). No pixels are decoded, and each distinct image
is asked for once. A frame that leaves out `source` gets a concrete rectangle covering its whole
image, so every loaded frame has a source rectangle. `decodeAnimationResource` takes the size
lookup as a function, so tests can supply a fake.

### Pivot convention

A pivot is in pixels relative to the frame's source rectangle and is placed on pixel *edges*:
- `[0, 0]` is the top-left corner of the top-left pixel;
- `[width, height]` is the bottom-right corner of the bottom-right pixel.

This is the convention room pivots and `SpriteBatcher` use. For a character standing on the
floor, put the pivot at the bottom-centre between its feet, for example `[12, 40]` for a 24 × 40
frame. Mirroring a frame mirrors its pivot to `width − pivot.x`, which is exact on the edge
convention.

## Ticks and the clock

Durations are whole **animation ticks**, at a fixed `animationTicksPerSecond` = 60. The engine has
no fixed-rate update: `Application` passes each frame's delta as double seconds, clamped to 0.25 s.
`AnimationClock::advance(seconds)` converts that to ticks:
- Each update's seconds are rounded once, to a billionth of a tick.
- The remainder below one tick carries over exactly, so nothing is truncated per update.
- 60 updates of 1/60 s give 60 ticks, and so do 144 updates of 1/144 s.
- One update of 2/60 s gives 2 ticks.
- Splitting an interval into several updates gives the same total, up to that rounding.

Playback therefore does not depend on the frame rate, the refresh rate, VSync, or how often
anything is drawn. Negative and non-finite deltas count as 0. A single delta over 10^6 s counts as
10^6 s.

## Playback

`AnimationPlayer` holds an `AnimationClipRef`: the shared resource plus a clip index. The clip
stays alive as long as any player uses it, even if the resource cache unloads it.

**Status** is one of:
- `Stopped`: on the first frame, not advancing (a new player is stopped with no clip);
- `Playing`;
- `Paused`;
- `Finished`: a non-looping clip has reached its end and stays on its last frame.

**Controls:**

| Call | Effect |
|---|---|
| `play(clip)` | Selects and plays `clip`. If `clip` is already selected and **playing or paused**, nothing changes (it returns false). Movement code may request the same clip every frame. If the clip is selected but **stopped or finished**, it starts again from the beginning. A different clip starts from its first frame. |
| `restart()` | The current clip from the beginning, playing. |
| `playbackGeneration()` | Names the current playback. It changes when a playback starts (`play` returning true, `restart`) or is stopped (`stop` from another status). Pause, resume, advancing and finishing keep it, so "same generation and Finished" means the playback that was started has finished. |
| `stop()` | Back to the first frame, not advancing. |
| `pause()` / `resume()` | Playing ↔ paused; no effect in other states. |
| `advance(ticks)` | Advances a playing clip. Returns `{finished}`, which is true only in the update where a non-looping clip completes, never again after that. |

**Advancing cost:**
- `advance` takes constant time however large `ticks` is: a looping clip skips whole loops with
  `elapsed = (elapsed + ticks % total) % total`.
- All values stay below 2^33, so nothing overflows.
- Finding the current frame is a binary search over the clip's frame start times.

**Saving state:** playback state is three plain values: the clip, the elapsed ticks and the
status. A future save system can store and restore them, naming the clip by resource path and
clip name, never by pointer.

## Not included

There are no animation events, blending, cross-fading, state machines, multiple tracks,
skeletal animation, or runtime atlas packing. Changing direction does not keep the phase of the
previous clip: see [Actors.md](Actors.md#facing-and-directional-animation).

## Tests

`Tests/Animation/AnimationTests.cpp` covers:
- the format and its validation, with a fake image-size function;
- loading from assets through image headers;
- every playback rule: boundaries, partial frames, multiple loops in one update, 2^40 ticks,
  completion firing once, pause, stop, restart, the same-clip rules, independent players, and a
  clip outliving its cache entry;
- the clock: frame-rate independence, fraction carry-over, split intervals and invalid deltas.
