# Continuous integration

The GitHub Actions workflow [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml) runs on
every push and pull request, and can be started by hand (`workflow_dispatch`).

## Jobs

There is one job per platform, each with a matrix. `fail-fast` is off, so one failing
configuration does not hide the others.

| Job | Runner | Configurations | Compiler | Setup |
|---|---|---|---|---|
| `windows` | `windows-2022` | Debug, Release | MSVC (Visual Studio 2022), preset `windows-msvc` | none: VS 2022 and CMake are on the image |
| `macos` | `macos-15` | Debug, Release | Apple Clang (Xcode), preset `macos-clang` | `brew install ninja` |
| `linux` | `ubuntu-24.04` | Debug, Release × GCC 14 / Clang 19 | presets `linux-gcc` / `linux-clang`, compiler overridden to `gcc-14`/`g++-14` or `clang-19`/`clang++-19` (not Ubuntu's default Clang 18, whose concepts level does not unlock `std::expected` in libstdc++ 14) | `apt-get install` of the packages in [Building.md](Building.md) (including `libopengl-dev`, which the Ubuntu image lacks), plus `xvfb`, `xauth`, `libgl1-mesa-dri` for the integration tests and `pulseaudio`, `pulseaudio-utils` for the audio tests |

Each job:
1. configures with the platform's configure preset;
2. builds with `<preset>-<config>`, which compiles the engine, EngineDemo and all tests with
   warnings as errors;
3. runs the unit tests, then the integration tests (Linux) or a summary step stating why they
   cannot run on that runner (Windows, macOS);
4. runs the audio tests: on Linux against a PulseAudio null sink, on Windows and macOS as an
   informational probe (see below).

The only actions used are GitHub's own `actions/checkout` and `actions/upload-artifact`.

## Tests

Every test has exactly one category, enforced by `schuimm_add_test` in `Tests/CMakeLists.txt`:

| Label | Needs | CI |
|---|---|---|
| `unit` | nothing: no display, GPU or network | all jobs; must pass |
| `integration` | a desktop session and an OpenGL 3.3 driver | Linux under Xvfb with Mesa llvmpipe (`LIBGL_ALWAYS_SOFTWARE=1`). **Not run on Windows or macOS.** |
| `audio` | an audio output device | Linux: a user PulseAudio daemon with `module-null-sink`; must pass. Windows, macOS: **informational probe**, does not decide the job. |

Neither hosted runner type can create the context the engine needs:
* **Windows** runners have no GPU driver, and `opengl32.dll` only offers Microsoft's OpenGL 1.1
  GDI renderer.
* **macOS** runners are virtual machines without a GPU. `NSOpenGLPixelFormat` finds no
  hardware-accelerated core profile, and the engine deliberately requests an accelerated one.
  This was observed on the first CI run on `macos-15`.

**Audio.** Hosted runners have no sound card. On Linux the job starts a PulseAudio daemon with a
null sink, which the audio stack treats as a real output device, so `AudioDeviceSmoke` plays
through miniaudio's PulseAudio backend and must pass. Hosted Windows and macOS runners do not
guarantee any output device. There the audio tests run with `continue-on-error`, and a following
step writes their outcome (passed: a device was found; failed: none) into the job summary,
stating that it does not affect the job. The mixer, the playback logic and the real miniaudio
device code (on its null backend) are part of the `unit` tests and therefore must pass on every
job.

Both jobs build the integration tests but do not run them. A step writes the reason and the list
of tests that were not run into the job summary, so the gap is visible rather than silent. On
those platforms the integration tests are run by hand (see [ManualTesting.md](ManualTesting.md)).

The tests run through [`.github/scripts/run-tests.sh`](../../.github/scripts/run-tests.sh), which:
* lists the CTest tests with the label (`ctest -N`);
* runs them with `--output-on-failure --verbose`, so every test case's `[ RUN ]` / `[ OK ]` /
  `[ FAIL ]` line is in the log;
* fails if no test has the label (the presets set `noTestsAction: error`), so a mistyped label
  cannot pass silently;
* writes a JUnit file to `Build/test-results/`, uploaded as an artifact;
* appends the executed tests and test cases to the job summary on the run page;
* exits with CTest's exit code, so any failing test fails the job.

## Caching

The workflow has no cache:
* All third-party code is vendored in `ThirdParty/`, so there is nothing to download.
* The system packages come from the runner's package manager.
* A complete build takes a few minutes (FreeType and miniaudio dominate the C part).

A compiler cache (ccache or sccache) becomes worthwhile when the build gets substantially slower.

## Extending

* **New subsystem with tests:** add `Tests/<Subsystem>/CMakeLists.txt` using
  `schuimm_add_test(... LABELS unit)` (or `integration`) and `add_subdirectory` it. The workflow
  selects tests by label, so it needs no change.
* **New system dependency:** add it to the platform's install step and to
  [Building.md](Building.md), so local and CI setups stay identical.
* **New test category** (as was done for `audio`): add the label to the allowed list in
  `schuimm_add_test` and a step per job that runs it, or reports explicitly why it cannot run
  there.

## Reproducing a CI job locally

```bash
cmake --preset linux-gcc -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14
cmake --build --preset linux-gcc-debug
.github/scripts/run-tests.sh linux-gcc-debug unit
.github/scripts/run-tests.sh linux-gcc-debug integration xvfb-run -a -s "-screen 0 1280x1024x24"
pulseaudio --start --exit-idle-time=-1 && pactl load-module module-null-sink sink_name=ci_null
.github/scripts/run-tests.sh linux-gcc-debug audio
```

On Windows, run the script from Git Bash.
