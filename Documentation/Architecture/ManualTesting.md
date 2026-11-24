# Manual integration testing

EngineDemo is the per-platform integration test. Automated parts run with `--self-test`; the rest
needs a person at the machine.

## Automated: `EngineDemo --self-test [--screenshot out.ppm]`

Runs 30 frames, then checks, in order, and logs `PASS`/`FAIL` for each step:

1. **opengl-context**: core profile, version ≥ 3.3.
2. **resize**: `setSize(960×540)`, the size is reported back and a resize event arrives.
3. **fullscreen-enter** and **fullscreen-leave**: state and event round-trip.
4. **vsync**: frame rate with the swap interval at 0 versus 1. It passes if "off" is more than
   20 % faster; if the driver or compositor forces sync, this is a warning, not a failure.
5. Optional screenshot of the composited back buffer (PPM), then clean shutdown.

Exit code 0 means all steps passed. Shader compilation, triangle, textured quad and alpha
blending are checked by inspecting the screenshot: a red/green/blue gradient triangle on the
left, an 8×8 warm checkerboard in the centre, glowing orbs blended over and beside it, and corner
markers red (top-left), green (top-right), blue (bottom-left), white (bottom-right).

## Manual checklist

Start `EngineDemo` normally and check:

| # | Check | How |
|---|---|---|
| 1 | Window opens with the scene | as described above |
| 2 | OpenGL version | log line `OpenGL version: …`, `Context: x.y core profile` |
| 3 | Keyboard | press/hold/release keys: log shows `Key X Pressed` / `Key X Released` once each, never repeated while held; arrow keys move the triangle smoothly while held |
| 4 | Modifiers left/right | LeftShift vs RightShift, LeftAlt vs RightAlt logged separately |
| 5 | Layout independence | with AZERTY active, the physical "Q" key still logs `Key A` (the US position) |
| 6 | Mouse | sprite follows the cursor, including in fullscreen and after resizing; left/right/middle buttons tint it; wheel scales it; log shows buttons and wheel |
| 7 | Resize | drag the border: content stays letterboxed and keeps animating during the drag (Windows/macOS) |
| 8 | Fullscreen | F11 / Alt+Enter (and Ctrl+Cmd+F on macOS) in and out; window position and size restored |
| 9 | VSync | press V: title FPS jumps from the display rate to hundreds or thousands, and back |
| 10 | Minimise | log shows `Suspended`, CPU drops to ~0 %; restore shows `Resumed` and no animation jump |
| 11 | Focus | hold an arrow key and Alt-Tab/Cmd-Tab away: the key is logged `Released`; `Window lost focus` |
| 12 | Close | window close button, Alt+F4 (Windows), ⌘Q (macOS): log ends with `Clean shutdown, exit code 0` |
| 13 | HiDPI | on a scaled display the scene is sharp and the framebuffer size in the title is the pixel size |

## Verification record (Milestone 1.5)

Machines: macOS 26.6 on Apple M4 Max (Apple Clang 21); Debian 13 GNOME/Xwayland on Intel UHD 630
(GCC 14.2, Clang 19.1); Windows 11 on NVIDIA RTX 4050 Laptop GPU (MSVC 19.41, VS 2022 Build
Tools). Each build was configured from an empty build directory.

| Check | macOS | Debian 13 | Windows 11 |
|---|---|---|---|
| Debug + Release build, zero warnings (`-Werror` / `/WX`) | ✅ | ✅ GCC and Clang | ✅ |
| `ctest -L unit`: CoreTests (54 cases), PlatformTests (2), FileSystemTests (13), ResourceTests (26) | ✅ | ✅ GCC and Clang, Debug and Release | ✅ Debug and Release |
| `ctest -L integration`: LifecycleTests (4 cases), EngineDemoSmoke | ✅ Debug and Release | ✅ under Xvfb (all four builds) and on the GNOME session | ✅ Debug and Release (desktop session via `schtasks /it`) |
| `EngineDemo --self-test` (Release) | ✅ GL 4.1 Metal | ✅ GL 4.6 Mesa | ✅ GL 3.3 NVIDIA |
| Clean shutdown, exit code 0 | ✅ also from fullscreen | ✅ | ✅ |
| No per-frame autorelease leak (`OBJC_DEBUG_MISSING_POOLS=YES`, 30 vs 600 frames) | ✅ count constant, none on the main thread | n/a | n/a |
| X protocol error is logged and not fatal (deliberate `XDestroyWindow` of a bogus id) | n/a | ✅ | n/a |
| `.github/scripts/run-tests.sh` | ✅ | ✅ including `xvfb-run` wrapper | ✅ under Git Bash |
| GitHub Actions: build + unit tests | ✅ hosted `macos-15` runner (integration: reported as not run) | ✅ GCC 14 on `ubuntu-24.04`, unit + integration under Xvfb; ☐ Clang job switched from 18 to 19 after the second run, not yet re-run | ✅ hosted `windows-2022` runner (integration: reported as not run) |

Notes:

* `LifecycleTests` also re-creates the OpenGL surface on the same window, which re-applies the
  pixel format on Windows. That works with the NVIDIA driver, so the suspected WGL problem was not
  reproduced and that code was left unchanged (see
  [KnownLimitations.md](KnownLimitations.md)).
* First CI run: Windows passed. On macOS the build and unit tests passed, but the hosted VM has
  no hardware-accelerated OpenGL, so the integration tests are now reported as not run there
  (like Windows). On Linux, configuration failed because the Ubuntu image lacks `libopengl-dev`
  (without it CMake creates no `OpenGL::GLX` target); the package was added to the workflow and
  to the documented package list.
* Second CI run: everything passed except the Clang 18 jobs. Ubuntu 24.04's default Clang 18
  cannot use `std::expected` from libstdc++ 14 (it requires `__cpp_concepts >= 202002`), so the
  job now uses Clang 19, which is also the version tested on Debian.
* Physical keyboard/mouse checks (checklist 3–7, 10, 11) were not repeated; Milestone 1.5 did not
  change input handling.

## Verification record (Milestone 1)

Machines: macOS 26.6 on Apple M4 Max; Debian 13 (GNOME on Wayland, X11 backend via Xwayland) on
Intel UHD 630; Windows 11 on NVIDIA RTX 4050 Laptop GPU (125 % scaling).

| Check | macOS | Debian 13 | Windows 11 |
|---|---|---|---|
| Debug + Release build, zero warnings | ✅ | ✅ GCC 14.2 and Clang 19 | ✅ MSVC 19.41 |
| `ctest` (51 unit tests, key map tests, smoke test) | ✅ | ✅ (smoke test under Xvfb) | ✅ |
| Window + context creation (logged version) | ✅ 4.1 Metal core | ✅ 4.6 Mesa core (Xvfb: 4.5 llvmpipe) | ✅ 3.3 NVIDIA core |
| Shader compilation, triangle, textured quad, alpha (screenshot) | ✅ | ✅ | ✅ |
| Self-test resize | ✅ | ✅ | ✅ |
| Self-test fullscreen enter/leave | ✅ | ✅ (mutter EWMH) | ✅ |
| Self-test VSync (off vs on FPS) | ✅ 3637 vs 104 | ✅ 2557 vs 61.5 | ✅ 2704 vs 146 |
| Clean shutdown (exit 0, resources released) | ✅ also via AppleEvent quit | ✅ | ✅ |
| Suspension while hidden | ✅ 0 % CPU while the screen was locked | unit-tested | unit-tested |
| Physical keyboard / mouse (checklist 3–6) | ◐ Space, Up, left button seen pressed/released; rest open | ☐ | ☐ |
| Live-resize redraw, minimise/restore, focus (7, 10, 11) | ☐ | ☐ | ☐ |

Notes:

* Physical input could not be exercised remotely: the machines were driven over ssh, and macOS
  desktop control lacked the Accessibility permission. The translation tables and the input state
  machine are unit-tested on every platform, but the checklist items marked ☐ still need a person
  at each machine.
* The macOS self-test was re-run after the occlusion change (hidden windows report as minimised)
  and passes. That change first left the demo stuck suspended at start-up, because AppKit reports a
  new window as occluded until it is on screen; this was found in a manual run and fixed by
  keeping `isMinimized()` and the events on one tracked state.
* Windows 7 and macOS 10.13 were not available; support there is by construction only
  (`_WIN32_WINNT=0x0601` with dynamically loaded newer APIs, `-mmacosx-version-min=10.13` with
  availability errors enabled).

## Running remotely

* **Windows over ssh** runs in a non-interactive session. To test on the visible desktop, launch
  through a scheduled task with `/it`, e.g. `schtasks /create /tn SchuimmDemo /tr C:\path\run-demo.cmd
  /sc once /st 23:59 /it /f` then `schtasks /run /tn SchuimmDemo`.
* **Linux over ssh**: `DISPLAY=:0 XAUTHORITY=/run/user/$UID/.mutter-Xwaylandauth.* EngineDemo --self-test`
  targets the logged-in GNOME session; `xvfb-run -a EngineDemo …` runs headless (no window
  manager, so fullscreen is unavailable and swap control is absent).
