# Fake X-Touch and device tests

A test-only `FakeXTouch`, in `jpr/device/testing`: the hardware end of a pair of
the fake REAPER's MIDI ports. It decodes what JPRSurf sends into the state of
the hardware (lights, faders, encoder rings, meters, scribble strips, and the
timecode display), and sends presses, touches, moves, and turns as the hardware
does. With it come tests of every `DeviceXTouch` control's inputs and outputs,
and of `Control`'s press timing and output bindings. The design is in
[testing_and_profiling.md](../testing_and_profiling.md) (Fake X-Touch).

There is no change in behavior, except for whatever the protocol checks below
show `DeviceXTouch` gets wrong.

## Design

### Names

| Name                                        | What                                                                                   | Might be confused with                                                                               |
| ------------------------------------------- | -------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| `FakeXTouch`                                | The hardware end of an X-Touch's (or extender's) ports in the fake REAPER              | `DeviceXTouch`, the device JPRSurf drives; `FakeReaper`, which owns the ports                        |
| `FakeXTouch::Button`, `StripButton`, `Led`  | The X-Touch's buttons: the global ones, the ones on each strip, and the lights alone   | `DeviceXTouch`'s control names (`kPlay`, `Mute(strip)`), which are strings naming `Control`s         |
| `FakeXTouch::Light`                         | What a light shows: off, on, or blinking                                               | `ControlDValueOutputMidiNote::Mode`, the modes JPRSurf writes lights in                              |
| `FakeXTouch::Ring`                          | What an encoder's light ring shows: its mode (0-7) and position                        | `ControlDValueOutputMidiCc::McuEncoder()`'s modes, which are the same 0-7, plus 8 for off            |
| `FakeXTouch::ScribbleColor`                 | One of the scribble strip's eight colors                                                | `Color` (`jpr/common/color.h`), the full RGB color JPRSurf maps to one                               |
| `FakeMidiOutput::Connect()`                 | Passes each message sent from the port to the hardware connected to it, as it is sent  | `MidiPorts::OpenOutput()`, which opens JPRSurf's end of the port                                     |

`Connect()` says what happens physically: the hardware is plugged into the
port. Alternatives were `SetReceiver()` (names the mechanism, not the meaning)
and `Attach()` (vaguer).

The buttons are named as `DeviceXTouch` names its controls, so a test reads the
same whichever end it works on. The names are all they share (see FakeXTouch).

### Connecting hardware to a fake port (common/testing)

```
class FakeMidiOutput final : public midi_Output {
  // Connects hardware to the port: each message sent from it is passed to
  // `receiver` as it is sent. A port has at most one device, so connecting
  // one while another is connected fails the test.
  void Connect(absl::AnyInvocable<void(absl::Span<const uint8_t> bytes)>
                   receiver);
  void Disconnect();

  // Sets whether the port records each message sent from it, for
  // TakeReceived(). It doesn't until a test asks it to.
  void SetRecording(bool recording);
};
```

- The fake X-Touch decodes each message as it is sent, so its state is always
  current, and a message it doesn't understand fails the test in the run that
  sent it. Its getters are `const`, and a test never has to remember to read
  the port first.
- Recording is the port's alone, and independent of the hardware connected to
  it, so a test can have either, both, or neither, and no fake device keeps a
  raw log of its own. It is off by default, so a test that never reads it
  keeps none, and taking from a port that isn't recording fails the test.

### FakeXTouch (device/testing)

```
class FakeXTouch final {
 public:
  enum class Type { kFull, kExtender };

  // Buttons on every strip (`strip` is 0-7), and the rest, which only the full
  // X-Touch has.
  enum class StripButton { kRec, kSolo, kMute, kSelect, kPotButton };
  enum class Button { kAssignTrack, ..., kPlay, ..., kRight };

  // Lights with no button.
  enum class Led { kSmpte, kBeats, kSolo };

  enum class Light { kOff, kOn, kBlinking };
  enum class ScribbleColor { kBlack, kRed, kGreen, kYellow, kBlue, kMagenta,
                             kCyan, kWhite };

  struct Ring {
    // 0-3: a dot, boost/cut, wrap, or spread. 4-7: the same, with the far left
    // and right lights lit (see To confirm).
    int mode = 0;
    int position = 0;  // 0 for none lit, or 1-11.
  };

  // Faders are 0-7 on each strip, and the master fader, on the full X-Touch.
  static constexpr int kMasterFader = 8;
  static constexpr int kFaderMax = 16383;  // Pitch bend's 14 bits.

  // Lists the X-Touch's input and output ports in `reaper`, as `port_name`, and
  // connects to the output. It must be destroyed before `reaper`.
  FakeXTouch(FakeReaper& reaper, Type type, std::string_view port_name);
  ~FakeXTouch();  // Disconnects from the output.

  // Into JPRSurf, delivered on the next MidiPorts::RunInput().
  void Press(Button button);
  void Press(StripButton button, int strip);
  void Release(Button button);
  void Release(StripButton button, int strip);
  void TouchFader(int fader);
  void ReleaseFader(int fader);
  void MoveFader(int fader, int position);  // 0 to kFaderMax.
  void TurnPot(int strip, int clicks);      // Clockwise is positive.

  // What the hardware shows: the last of each sent to it.
  Light GetLight(Button button) const;
  Light GetLight(StripButton button, int strip) const;
  Light GetLight(Led led) const;
  int GetFader(int fader) const;
  Ring GetRing(int strip) const;
  int GetMeter(int strip) const;  // 0x0-0xE, falling one level each run.
  std::string GetScribble(int strip, int line) const;  // 7 characters.
  ScribbleColor GetScribbleColor(int strip) const;
  std::string GetTimecode() const;  // 10 digits, each lit dot a '.' after it.

  // The output port, where a test that checks raw messages records them.
  FakeMidiOutput* GetOutputPort() const;
};
```

- **Written from the protocol, not from `DeviceXTouch`.** Its tables come from
  the Mackie Control protocol the X-Touch speaks in MCU mode (button notes,
  pitch bend faders, encoder CCs and rings, channel pressure meters, and the
  timecode CCs) and the X-Touch's sysex for scribble text and colors. The
  library links `jpr_common_testing` but not `jpr_device`, so it can't reuse
  `DeviceXTouch`'s tables, and a misreading of the protocol isn't copied into
  both. Where the X-Touch differs from the Mackie it emulates (meters, and the
  ring's end lights), the fake follows the X-Touch, and the hardware settles
  anything not yet known (see To confirm).
- **Strict.** Anything the hardware wouldn't accept fails the test, naming the
  message: a note with no light, a velocity other than off (0), blinking (1),
  or on (127), a meter level of 0xF, a ring position past 11, a control the
  extender doesn't have, a scribble or color sysex with the other model's
  device ID (0x14 is the X-Touch, 0x15 the extender), text past the end of the
  display, or any message the fake doesn't decode yet
  (such as the jog wheel or the assignment display, which JPRSurf doesn't use).
  Supporting a new one is a change to the fake.
- **Inputs** are sent as the hardware sends them: a button or fader touch is a
  note on with velocity 127, and a release the same note at 0. A move is a
  pitch bend on the fader's channel (8 for the master). A turn is CC 0x10 +
  strip, with the clicks in bits 0-5 and bit 6 set for counterclockwise; more
  than 63 at once fails the test. Using a control the extender doesn't have
  fails the test.
- **Outputs** are held as the hardware shows them, and start off, at zero, and
  blank. A fader shows the last position sent or moved to. A meter falls, as
  on the hardware, until another level is sent: one level for each run (1/30
  second) of the fake's clock. That's faster than the hardware, but close
  enough, and it lets a test see at once that a meter wasn't sent again on the
  next run, such as a track's meter while the transport plays. Scribble
  text is written at its offset into the two 56 character lines, so a strip's
  line is 7 characters of it. The timecode's digits are decoded from the MCU's
  display characters: codes 0x00-0x1F are `@` to `_`, 0x20-0x3F are ASCII, and
  0x40 lights the dot.
- **Performance:** test only, never linked into the plugin.

**Brittleness:**
- The fake must be destroyed before the `FakeReaper` that owns its ports, as
  anything that uses the fake must. Declaring it after the fake, as fixtures
  already do, gets this right.
- `MidiPorts` sends its last output when it is destroyed, so the fake X-Touch
  should outlive it too, or it doesn't see that output. Nothing fails either
  way.

### Device tests (device)

A fixture in `device_xtouch_test.cc`, run as `PluginSurface` runs its devices:

```
class DeviceXTouchTest : public ::testing::Test {
 protected:
  // Runs the device, then MIDI input and output, as PluginSurface::OnRun()
  // does, and advances the fake's clock by one run.
  void Run();

  FakeReaper reaper_;
  FakeXTouch xtouch_{reaper_, FakeXTouch::Type::kFull, "X-Touch"};
  MidiPorts ports_;
  Runner runner_{"Device"};
  DeviceXTouch device_{DeviceXTouch::Type::kFull, runner_,
                       ports_.OpenInput("X-Touch"),
                       ports_.OpenOutput("X-Touch")};
};
```

- **`DeviceXTouch`:** every control, on both models, table driven where the
  controls repeat. Buttons (press, release, and lights on and blinking), faders
  (moves through the MCU curve's points, touch, motor output held while
  touched, the master fader on the full X-Touch only, and the extender's third
  fader without touch), pots (turns, and each ring mode, and cleared), meters
  (each level, and held while they are sent each run), scribble text on both
  lines, colors (the RGB to palette mapping, all eight in one message), the
  timecode in each timeline mode and as text, and the SMPTE, Beats, and Solo
  lights. A few tests check the raw messages too, recorded on the output port:
  one of each kind, and the sysex prefix for each model.
- **`Control`:** what `DeviceXTouch`'s controls exercise, against the fake
  clock: press, long press, double press, and tap, alone and as siblings;
  modifiers choosing between registrations; a bound output held while touched,
  and delayed after input with no touch (dependent and motorized); and outputs
  cleared when the last writer goes, but not when another replaces it.
- **With the config work:** once devices can be created in tests, *Device types
  and catalogs*' check that a device's controls match its catalog becomes a
  unit test, as well as a check at startup, and *Build the scene from a
  SurfaceSpec*'s building is unit tested too (see "With the config work" in the
  design doc).

### To confirm

`DeviceXTouch` and the Mackie protocol disagree on four things, and the fake
follows the X-Touch. Two are already known:
- **Meters:** the X-Touch's meters differ from the Mackie's, and
  `DeviceXTouch`'s levels (0xD for -4 dB and up, 0xE for clipping) were tested
  extensively on it. The fake holds the level as sent, with no overload light.
- **Ring bit 6:** the X-Touch has no center light below the ring, as the Mackie
  does. Bit 6 lights the two end lights instead (far left and far right), as
  `DeviceXTouch`'s comment says.

The other two were checked in REAPER (CL1). Each finding goes into the fake's
tables (CL3), and the fixes into `DeviceXTouch` (CL4).
- **Master fader touch is note 0x70**, as the protocol (and Klinke) have it,
  not the 0x67 `DeviceXTouch` listens for. The master fader's touch has never
  worked, so its motor output isn't held while it is touched. CL4 fixes it.
- **The timecode follows the protocol's codes.** Sent as codes (0x00-0x1F),
  `@` to `_` show without dots. Sent as their ASCII (0x40-0x5F), as
  `DeviceXTouch` sends them, each shows its dot too, as bit 6 is the dot. CL4
  fixes `DeviceXTouch` to send the codes. `@` shows as a blank, and `\` as a
  squiggle, either way; the fake reports the character sent, not how the
  segments draw it. ASCII 0x20-0x3F (digits, space, and punctuation) is the
  same either way.

## CLs

### CL1 [x] REAPER: Check the protocol against the hardware

Depends on: nothing.

- Temporary code, in the main checkout: `MidiIn::Poll()` logs every message
  from the X-Touch, and a spare button (F8) shows the next of a few letter
  patterns on the timecode display, each sent once as ASCII and once as the
  protocol's codes, logging what it sent.
- The user touches the master fader and presses through the patterns, and says
  what each step shows.
- Commits only the findings, in To confirm above, and any change to the later
  CLs they call for.

**Verify**
- The temporary code is gone: `git diff` shows only this plan.

### CL2 [x] common/testing: Connect hardware to a fake output port

Depends on: nothing.

- `FakeMidiOutput::Connect()` and `Disconnect()`, and their tests in
  `fake_midi_test.cc`: a connected port passes each message (short and sysex)
  to the receiver as it is sent, and keeps none for `TakeReceived()`;
  connecting a second device fails the test; and disconnecting goes back to
  keeping them, after which another device can connect.
- Unused, so no visible change.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL2b [x] common/testing: Record a fake output port independently

Depends on: CL2.

Added after CL3's review, which found the fake X-Touch keeping a second raw
log, because a connected port stopped recording.

- `FakeMidiOutput::SetRecording()`: recording is independent of `Connect()`,
  off by default, and `TakeReceived()` on a port that isn't recording fails the
  test. `fake_midi_test.cc` tests each combination, and `midi_port_test.cc` and
  `midi_ports_test.cc` turn recording on.
- Unused by the plugin, so no visible change.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL3 [x] device/testing: FakeXTouch

Depends on: CL1, CL2b.

- The `jpr_device_testing` library (`src/jpr/device/testing/`), linking
  `jpr_common_testing` and not `jpr_device`.
- `FakeXTouch`, its protocol tables, as CL1 settled them, and
  `fake_xtouch_test.cc`, which checks it against raw bytes alone: each input
  sends the protocol's message on the input port, each kind of output message
  changes the state it should, meters fall each run until sent again, and each
  strictness check fails the test (`EXPECT_NONFATAL_FAILURE`), on both models.
- The design doc's Fake X-Touch section: what was built, with the example test
  in the final API.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL4 [ ] device: DeviceXTouch tests

Depends on: CL3.

- `device_xtouch_test.cc`, with the fixture, and `jpr_device_TEST_SOURCE`
  linking `jpr_device_testing`.
- Tests of every control on both models (see Device tests).
- Fixes to `DeviceXTouch` for what CL1 found, which its tests show: the master
  fader's touch is note 0x70, and the timecode sends `@` to `_` as the
  protocol's codes (`EncodeChar()` keeps the low 6 bits). And the stray
  `#pragma once` in `device_xtouch.cc`.
- `docs/backlog.md`: *Device types and catalogs* and *Build the scene from a
  SurfaceSpec* as "With the config work" describes.

**Verify**
- Standard checks.
- Each fix, by hand, in Checks in REAPER below.

### CL5 [ ] device: Control tests

Depends on: CL4.

- `control_test.cc`, on `DeviceXTouch`'s controls and the CL4 fixture (moved to
  a shared test header if both files use it): press timing, modifiers,
  bindings, and output writers (see Device tests).
- Anything the tests find wrong in `Control` is fixed in its own follow-up CL,
  not here.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

## Checks in REAPER

- The master fader's touch: touch the master fader and move it while the
  volume changes in REAPER (play automation), and it doesn't fight the hand;
  let go, and it follows the volume again.
- The timecode still shows each timeline mode as before, with its dots. Nothing
  sends it letters yet, so the letter fix is only checked by its tests.
- The extension loads with no new errors in `jprsurf.log`, and the smoke test
  passes. There is no idle or smoke profile: nothing on the realtime path
  changes.
