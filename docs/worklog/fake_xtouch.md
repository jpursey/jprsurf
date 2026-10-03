# Fake X-Touch and device tests

A test-only `FakeXTouch`, in `jpr/device/testing`: the hardware end of a pair of
the fake REAPER's MIDI ports. It decodes what JPRSurf sends into what the
hardware shows (lights, faders, encoder rings, meters, scribble strips, and the
timecode display), and sends presses, touches, moves, and turns as the hardware
does. With it, every file in `device` has its own tests: each `DeviceXTouch`
control on the fake X-Touch, and `Control`, its inputs and outputs, their
handles, `Device`, and the MIDI inputs and outputs on their own. The design is
in [testing_and_profiling.md](../testing_and_profiling.md) (Fake X-Touch).

## Behavior

Checking the protocol against the hardware found two things `DeviceXTouch` had
wrong, which are fixed:
- **The master fader's touch works.** It is note 0x70, not the 0x67
  `DeviceXTouch` listened for, so the master fader's motor never held while it
  was touched. Now it doesn't fight the hand, as the strip faders didn't.
- **Letters on the timecode display show without dots.** The display's codes
  are the low 6 bits of the character's ASCII, with bit 6 the dot, and
  `DeviceXTouch` sent `@` to `_` as their ASCII, which lit each one's dot.
  Nothing sends the display letters yet, so only its tests show it.

Nothing else changes. `ControlDeltaInputMidiCcOnesComp` is renamed
`ControlDeltaInputMidiCcSignMagnitude`, as it decodes sign-magnitude (bit 6 is
the sign), not ones' complement.

## Names

| Name                                        | What                                                                                   | Might be confused with                                                                         |
| ------------------------------------------- | -------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| `FakeXTouch`                                | The hardware end of an X-Touch's (or extender's) ports in the fake REAPER              | `DeviceXTouch`, the device JPRSurf drives; `FakeReaper`, which owns the ports                  |
| `FakeXTouch::Button`, `StripButton`, `Led`  | The X-Touch's buttons: the global ones, the ones on each strip, and the lights alone   | `DeviceXTouch`'s control names (`kPlay`, `Mute(strip)`), strings naming `Control`s             |
| `FakeXTouch::Light`                         | What a light shows: off, on, or blinking                                               | `ControlDValueOutputMidiNote::Mode`, the modes JPRSurf writes lights in                        |
| `FakeXTouch::Ring`                          | What an encoder's light ring shows: its mode (0-7) and position                        | `ControlDValueOutputMidiCc::McuEncoder()`'s modes, the same 0-7, plus 8 for off                |
| `FakeXTouch::ScribbleColor`                 | One of the scribble strip's eight colors                                               | `Color` (`jpr/common/color.h`), the full RGB color JPRSurf maps to one                         |
| `FakeMidiOutput::Connect()`                 | Plugs hardware into the port: each message sent from it is passed to the hardware      | `MidiPorts::OpenOutput()`, which opens JPRSurf's end of the port                               |
| `FakeMidiOutput::SetRecording()`            | Whether the port records what is sent from it, for `TakeReceived()`                    | `Connect()`, which is independent of it                                                        |
| `FakePressInput`, `FakeValueInput`, ...     | Control inputs a test drives, and outputs that keep what they were last set to         | The MIDI inputs and outputs (`control_input_midi.h`), which a device builds controls from      |

## Structure

### common/testing: Hardware on a fake port (fake_midi.h)

`FakeMidiOutput::Connect()` passes each message sent from the port to the
hardware's receiver as it is sent, so the hardware's state is always current,
and a message it can't take fails the test in the run that sent it. A port has
at most one device: connecting a second fails the test. `Disconnect()` unplugs
it.

Recording (`SetRecording()`, `TakeReceived()`) is the port's own, independent
of `Connect()`, so a test can have either, both, or neither, and no fake device
keeps a raw log of its own. It is off until a test turns it on, and taking from
a port that isn't recording fails the test.

### common/testing: Runs (fake_reaper.h)

`FakeReaper::GetRunTime()` is one run's length (1/30 second), and
`GetRunCount(duration)` the runs in a duration, rounded up in integers so a
whole number of seconds is exact. `TestControlSurface::RunFor()`, the device
tests, and the control tests share them.

### device/testing: FakeXTouch (fake_xtouch.h)

- **Written from the protocol, not from `DeviceXTouch`.** Its tables come from
  the Mackie Control protocol the X-Touch speaks in MCU mode, and the X-Touch's
  scribble sysex. `jpr_device_testing` links `jpr_common_testing` but not
  `jpr_device`, so a misreading of the protocol isn't copied into both. Where
  the X-Touch differs from the Mackie, the fake follows the X-Touch (see
  X-Touch facts).
- **Strict.** Anything the hardware wouldn't take fails the test, naming the
  message (in hex) and why: a note with no light, a velocity other than off
  (0), blinking (1), or on (127), a meter level of 0xF, a ring position past
  11, a sysex with the other model's device ID, text past the display's end, a
  control the model doesn't have, or a message the fake doesn't decode yet
  (such as the jog wheel or the assignment display, which JPRSurf doesn't use).
  Supporting a new one is a change to the fake.
- **Inputs** are sent as the hardware sends them, at the fake's time, and
  arrive on the next `MidiPorts::RunInput()`: a button or touch is a note on at
  127, and its release the same note at 0; a move is a pitch bend on the
  fader's channel; a turn is the encoder's CC in sign-magnitude, up to 63
  clicks.
- **Outputs** start off, at zero, blank, and black. A fader shows the last
  position sent, or where a hand moved it, and goes back where it was sent
  when its touch is released. A meter falls one level each run until it is
  sent again. Scribble text is written at its offset in the two 56 character
  lines. The timecode reads back the character sent, with `.` after each lit
  dot, whether or not the display can draw it.
- `GetOutputPort()` is the port, for a test that checks the raw messages.

It must be destroyed before the `FakeReaper` that owns its ports, and should
outlive `MidiPorts`, which sends its last output when it is destroyed.

### device: DeviceXTouch (device_xtouch.cc)

The MCU's button and fader touch notes are configs on `ControlPressInputMidiMsg`
(`McuButton()`, `McuFaderTouch()`, and `McuMasterFaderTouch()`), beside the
MCU's lights, encoders, and faders on their own classes. `EncodeChar()` keeps a
character's low 6 bits.

### device: Fake control inputs and outputs (fake_control_io.h)

A header in `jpr_device`'s test sources (it can't go in `jpr_device_testing`,
which doesn't link `jpr_device`):
- `FakePressInput` (with or without release), `FakeValueInput`, and
  `FakeDeltaInput` make their base's protected input calls public.
- `FakeCValueOutput`, `FakeDValueOutput`, `FakeTextOutput`, and
  `FakeColorOutput` keep the value and mode they were last set to, and how
  many times they were set. The text output keeps a timeline position as it
  is, rather than formatting it, so no REAPER is needed.

## Tests

| File                            | Tested against                       | What                                                                                                                                                         |
| ------------------------------- | ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `fake_midi_test.cc`             | The fake                             | Connecting hardware, recording, and both together                                                                                                            |
| `fake_xtouch_test.cc`           | Raw bytes on the fake's ports        | Each input's message, each output's decoding, meters falling, released faders going back, and each strictness check, on both models                          |
| `device_xtouch_test.cc`         | The fake X-Touch                     | Every control on both models: buttons, lights, faders (touch, master, extender's third), pots, rings, meters, scribble strips, timecode, raw messages        |
| `control_test.cc`               | Fake inputs and outputs, own clock   | Press timing (with and without release), modifiers, values and deltas, outputs, bindings, and output writers                                                 |
| `control_input_test.cc`         | Fake inputs                          | Listeners, presses and releases, deltas                                                                                                                      |
| `control_output_test.cc`        | Fake outputs, the fake for the ruler | Clamping, timeline modes, default timeline text, cleared values                                                                                              |
| `control_*_handle_test.cc`      | A control of fakes                   | Moving handles, and unregistering                                                                                                                            |
| `device_test.cc`                | Fake outputs                         | Finding controls by name, their order, and taken names                                                                                                       |
| `control_input_midi_test.cc`    | Raw MIDI on the fake's ports         | Press and release messages, sign-magnitude deltas, the MCU fader curve, and the MCU configs                                                                  |
| `control_output_midi_test.cc`   | Raw MIDI on the fake's ports         | Notes in each mode, CC and channel pressure modes, every ring mode, the MCU fader curve, and the MCU configs                                                 |

## X-Touch facts

Checked on the hardware, and held by the fake:
- **Meters** show levels 0x0-0xE, with no overload light, unlike the Mackie's.
  `DeviceXTouch`'s levels (0xD for -4 dB and up, 0xE for clipping) were tuned
  on it.
- **Ring bit 6** lights the far left and right lights. There is no center
  light, as the Mackie has.
- **The master fader's touch** is note 0x70, after the strips' 0x68-0x6F.
- **Timecode codes** 0x00-0x1F are `@` to `_`, and 0x20-0x3F are ASCII, with
  0x40 the dot. `@` shows blank, and `\` as a squiggle.
- **A released fader goes back** to where it was last sent, by itself, about a
  second after release, even with REAPER not running, and never while touched.
  A fader whose touch doesn't work (the user's extender's third) goes back once
  it hasn't moved for about a second. So `MidiOut` not resending a position the
  fader was already sent is right. The fake puts it back at once, as the delay
  doesn't matter to JPRSurf, and leaves a fader moved without a touch where it
  is.

## Building blocks

- **`FakeXTouch`** for any test through a device: *Scene tests* and *Surface
  tests* drive the surface through it, as a user does, and read what it shows.
- **`FakeMidiOutput::Connect()`** for fake hardware of any other kind, and
  `SetRecording()` for a test of the raw messages.
- **`fake_control_io.h`** for testing anything built on controls without a
  device.
- **`FakeReaper::GetRunTime()` and `GetRunCount()`** for a test that runs on
  REAPER's cadence.

## Performance

Test only. The fixes change no per-run work, so no profile was taken.
