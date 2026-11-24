# Audio

## Overview

| Part | Library | Notes |
|---|---|---|
| `SoundData`: decoded PCM (float, mono or stereo, any rate) | `schuimm_core` (`Core/Audio`) | CPU-side, like `Image` |
| `decodeWav`, `loadSound`: WAV decoding, `ResourceCache<SoundData>` loader | `schuimm_resource` (`Resource/Audio`) | no audio API |
| `AudioDevice`, `Sound`, `SoundInstance`, `Mixer`, `AudioBackend` | `schuimm_audio` (`Source/Audio`) | no audio API; testable headless |
| `createMiniaudioAudioDevice`: output through miniaudio | `schuimm_audio_miniaudio` (`Source/Audio/Miniaudio`) | the only user of `miniaudio.h` |

The audio layer is independent of graphics. The application layer only knows the
`AudioDevice` interface: `Application::Services::audio`, which is optional.

## Choice of backend

SCHUIMM uses **miniaudio's low-level device API** (`ma_context`, `ma_device`) behind its own
interface. Everything else is SCHUIMM code: the mixer, voices, decoding, resampling and the
threading contract.

Alternatives considered:
* **Native backends** (WASAPI, CoreAudio, ALSA/PulseAudio/PipeWire): full control, but three
  device implementations to write and maintain, and Linux alone has several audio stacks.
  miniaudio already implements them in one maintained, single-file C library.
* **miniaudio's high-level engine** (`ma_engine`, `ma_sound`): less code, but its own mixer,
  decoders, resource manager and threads would decide allocation, threading and formats, and
  would be hard to keep behind a SCHUIMM interface. All of that is compiled out
  (`MA_NO_ENGINE`, `MA_NO_NODE_GRAPH`, `MA_NO_RESOURCE_MANAGER`, `MA_NO_DECODING`,
  `MA_NO_ENCODING`, `MA_NO_GENERATION`).

Backends compiled in: WASAPI and DirectSound (Windows), CoreAudio (macOS), PulseAudio and ALSA
(Linux; PipeWire is reached through its PulseAudio server), and the null backend.
* On Linux the libraries are loaded at run time, so building needs no audio packages.
* On macOS the frameworks are linked directly (`MA_NO_RUNTIME_LINKING`), so every CoreAudio call
  is checked against the 10.13 deployment target at compile time.

miniaudio is public domain or MIT-0 (see [`ThirdParty/README.md`](../../ThirdParty/README.md)).

Replacing miniaudio later means implementing `AudioBackend` (start, stop, sample rate, names)
for another API; the mixer and the API above it stay.

## Formats

`decodeWav` supports RIFF/WAVE files with:
* uncompressed PCM: 8-bit unsigned, or 16-, 24- or 32-bit signed integer;
* 32-bit IEEE float;
* `WAVE_FORMAT_EXTENSIBLE` when its sub-format GUID is exactly `KSDATAFORMAT_SUBTYPE_PCM` or
  `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT` (all 16 bytes are compared);
* mono or stereo at 1–384 kHz.

The decoder is strict about what it accepts. Each of these fails with `InvalidData`, and the
message names the file and the reason:
* compressed codecs (ADPCM, µ-law, MP3-in-WAV, ...) and unknown sub-format GUIDs;
* more than two channels, or a block alignment that does not match channels × sample size;
* in `WAVE_FORMAT_EXTENSIBLE`:
  * an extension shorter than 22 bytes;
  * valid bits per sample outside 1..container size (float: exactly 32);
  * a channel mask that assigns more speakers than there are channels (fewer is allowed);
* a data chunk that is not a whole number of frames, or holds no frames;
* a second `fmt ` chunk before the data, and truncated chunks.

Fewer valid bits than the container (20 bits in 24, 24 in 32) are supported. The format stores
them left-aligned with the padding bits low, so decoding at the container size gives the
correct values. Unknown chunks are skipped.

No other formats (Ogg Vorbis, FLAC, MP3) yet; see [KnownLimitations.md](KnownLimitations.md).
A decoder would be added next to `WavDecoder` and produce `SoundData`.

## API

```cpp
auto audio = createMiniaudioAudioDevice({}, logger);          // or nullptr: run silently
ResourceCache<SoundData> sounds(locator, loadSound);
auto chime = (*audio)->createSound(**sounds.load(chimeId));   // convert once, at load time
SoundInstance i = (*audio)->play(*chime, {0.8f /* volume */, false /* loop */});
(*audio)->pause(i); (*audio)->resume(i); (*audio)->setVolume(i, 0.5f); (*audio)->stop(i);
(*audio)->state(i);              // Playing, Paused or Stopped
(*audio)->setMasterVolume(0.7f);
```

* `createSound` converts `SoundData` to the device format on the calling (main) thread:
  stereo float at the device's sample rate, with linear-interpolation resampling. Playback then
  only copies samples.
* Up to 32 voices play at once (`Mixer::voiceCount`). `play` returns an invalid instance when
  all are busy.
* `SoundInstance` is a value handle `{voice, generation}`. After its playback ends it is
  stale: `state()` reports `Stopped` and control calls are ignored, even if the voice has
  been reused.
* Per-playback volume and master volume are linear gains. Changes are ramped over one audio
  buffer to avoid clicks. The mix is clamped to [−1, 1].
* `pauseAll` / `resumeAll` silence the output without changing the playbacks' own states. The
  `Application` calls them when it is suspended and resumed.
* `update()` reclaims voices whose playback ended and releases their sounds. The `Application`
  calls it once per frame.

## Threading model

Two threads touch audio:

* **Main thread** (owner): every `AudioDevice` call, loading and decoding (`loadSound`) and
  conversion (`createSound`). Nothing here waits for the audio thread.
* **Audio thread** (miniaudio's): calls `Mixer::mix()` once per period (about 10 ms requested)
  and nothing else.

They communicate without locks, through fixed state that cannot overflow:

* **Main → audio:** per voice, atomics for the playback's generation, sound, loop flag, desired
  state (playing, paused or stopped) and volume; plus the master volume and "all paused".
  Controls are *state*, not events: `mix()` reads the latest values once per period, and a newer
  value simply supersedes an older one. No control call can be dropped, so `state()` and
  `volume()` always report what the next period applies.
* **Audio → main:** per voice, an atomic "ended generation" is written when a playback finishes or
  has been stopped.

A new playback is published by writing its sound, loop flag, volume and state, and then its
generation (release). `mix()` reads the generation (acquire) and starts a playback whenever it
differs from the one it last started on that voice. Play requests are therefore never lost or
merged:
* Every `play()` takes a different free voice. Several plays in one frame use several voices
  and all start in the same period.
* A voice is free again only after its ended generation equals its last generation. `mix()`
  writes that only after it has started and finished that playback, so a published playback can
  never be overwritten before the audio thread has seen it.
* A new generation always differs from the voice's previous one, also when the 32-bit counter
  wraps (checked by an assertion).
* A play followed by a stop before the next period is registered and ended at once, so its voice
  is freed by `update()` and nothing of it is heard.

The only refusal is reported: when all 32 voices are busy, `play()` returns an invalid
instance.

`mix()` therefore never blocks, allocates, frees memory or performs I/O. Voices refer to their
sound through a raw pointer. The owning `std::shared_ptr` stays in the main thread's voice
slot, which releases it in `update()` only after the ended generation shows that the audio thread
no longer reads the sound. Freeing therefore never happens on the audio thread.
ThreadSanitizer runs of the audio tests report no data races (see
[ManualTesting.md](ManualTesting.md)).

## Ownership and lifetime

* `AudioDevice` owns the backend stream and the mixer. Its destructor stops the stream first
  (miniaudio joins its thread), then releases everything.
* Sounds are immutable and shared (`std::shared_ptr<const Sound>`). They hold no reference to
  the device and may outlive it. A sound created for one device plays only on a device with the
  same sample rate.
* The device must be destroyed before the platform layer. The demo destroys it after the
  application and before the graphics device (see [Lifecycle.md](Lifecycle.md)).
* There is no global audio state; several devices could coexist.

## Without an output device

`createMiniaudioAudioDevice` fails (`Unsupported`) when no platform backend can open an output
device. It does not fall back silently to the null backend. Applications decide what to do; the
EngineDemo logs a warning and runs without sound (`--no-audio` does the same deliberately).

`AudioDeviceSettings{.nullBackend = true}` selects miniaudio's null backend: a real audio thread
that consumes samples at the device rate without hardware. The headless unit tests use it, so
the actual miniaudio code path runs on every CI job.
