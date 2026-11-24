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
* `WAVE_FORMAT_EXTENSIBLE` when its sub-format is PCM or float;
* mono or stereo at 1–384 kHz.

Unknown chunks are skipped. Compressed codecs (ADPCM, µ-law, MP3-in-WAV, ...), more than two
channels and truncated or inconsistent files fail with `InvalidData`. The message names the
file and the reason.

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

They communicate without locks:

* **Main → audio:** commands (play, stop, pause, resume, volume, master volume, pause all) go
  through a fixed-capacity (1024) single-producer/single-consumer ring buffer of atomics
  (`SpscQueue`). `mix()` applies all queued commands at the start of each period. The queue never
  allocates. If it is full, the command is dropped and a warning logged; that takes more than
  1024 commands within one audio period.
* **Audio → main:** per voice, an atomic "ended generation" is written when a playback finishes or
  has been stopped.

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
