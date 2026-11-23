# Input handling

## Flow

```text
native event ─► backend translates ─► KeyEvent / MouseButtonEvent / MouseMovedEvent / MouseWheelEvent
            (scan code → Key)          queued on the Window
                                          │ Application::dispatch (after pollEvents)
                                          ▼
                                     InputState  ──update() once per frame──►  queries
```

## Keys are physical

`Key` names physical positions (US-QWERTY labels). Backends translate layout-independent codes:

| Platform | Source | Table |
|---|---|---|
| Windows | scan code from `lParam` bits 16–23 + extended bit 24 | `Win32KeyMap.cpp` |
| macOS | `NSEvent.keyCode` (kVK_* virtual key codes) | `CocoaKeyMap.cpp` |
| X11 | XKB key names (`AC01`, …) with keysym fallback | `X11KeyMap.cpp` |

So `Key::W` is always the key above `S`, on AZERTY or Dvorak too, which suits movement and shortcut
bindings. Text entry (characters, IME) is a separate concern for a later milestone. Keypad keys and
a few rare keys are not in `Key` yet and arrive as `Key::Unknown` (ignored).

## States

`InputState::update()` turns the edges seen since the previous frame into a per-frame
`ButtonState`:

| State | Meaning |
|---|---|
| `Up` | not down this frame or the previous one |
| `Pressed` | went down this frame (`wasPressed`) |
| `Held` | down this frame and the previous one (`isHeld`) |
| `Released` | went up this frame (`wasReleased`) |

`isDown()` is `Pressed || Held`. Rules covered by unit tests:

* Auto-repeat key-downs for a key that is already down are ignored (all backends also flag them as `repeat`).
* A press and release within one frame is reported as `Pressed`, then `Released` the next frame, so short taps are never lost.
* A release and re-press within one frame is a new `Pressed`.
* Focus loss calls `releaseAll()`, so keys held while switching applications come back `Released`.

## Mouse

* Position in window coordinates (logical units, origin top-left), `mouseDelta()` per frame, and
  `wheelDelta()` accumulated per frame in "lines" (+y away from the user; trackpad pixels on
  macOS are scaled by 0.1).
* Buttons `Left`, `Right`, `Middle`, `Extra1`, `Extra2` follow the same state machine. Windows
  captures the mouse while a button is down so releases outside the window arrive.
* `windowToVirtual()` maps a window position into the renderer's virtual resolution, accounting
  for HiDPI and letterbox bars. The demo's sprite uses it to follow the cursor.

## Testing without a device

`InputStateTests.cpp` drives `InputState` with synthetic events frame by frame; it acts as the
mock input implementation. `ApplicationTests.cpp` goes one level higher: a `MockWindow` queues
`KeyEvent`s and focus changes, and the tests check what the listener observes through `Application::input()`.
