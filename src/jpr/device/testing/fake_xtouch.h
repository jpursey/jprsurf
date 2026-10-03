// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/types/span.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {

//==============================================================================
// FakeXTouch
//
// A Behringer X-Touch or X-Touch Extender in Mackie Control (MCU) mode, as the
// hardware end of a pair of the fake REAPER's MIDI ports. It decodes what is
// sent to it into what the hardware shows, and sends presses, touches, moves,
// and turns as the hardware does.
//
// It is written from the Mackie Control protocol, and from what the X-Touch
// does differently, rather than from DeviceXTouch, so a misreading of the
// protocol isn't copied into both:
// - The meters show levels 0x0-0xE, with no overload light.
// - The rings have no center light. Bit 6 lights the far left and right lights
//   instead.
// - Scribble text and colors are the X-Touch's sysex, with device ID 0x14 for
//   the X-Touch, and 0x15 for the extender.
//
// It is strict: anything the hardware wouldn't accept, or that the fake
// doesn't decode yet, fails the test, naming the message. So does using a
// control the model doesn't have.
//
// It must be destroyed before the FakeReaper that owns its ports.
//==============================================================================

class FakeXTouch final {
 public:
  //----------------------------------------------------------------------------
  // Controls
  //----------------------------------------------------------------------------

  // The X-Touch, with its master section, or the extender, with just strips.
  enum class Type { kFull, kExtender };

  // The buttons on each of the eight strips, which both models have.
  enum class StripButton { kRec, kSolo, kMute, kSelect, kPotButton };

  // The rest of the buttons, which only the X-Touch has.
  enum class Button {
    kAssignTrack,
    kAssignSend,
    kAssignPan,
    kAssignPlugin,
    kAssignEQ,
    kAssignInst,
    kBankLeft,
    kBankRight,
    kChannelLeft,
    kChannelRight,
    kFlip,
    kGlobal,
    kShowNameValue,
    kShowTimeBeats,
    kF1,
    kF2,
    kF3,
    kF4,
    kF5,
    kF6,
    kF7,
    kF8,
    kViewMIDI,
    kViewInputs,
    kViewAudio,
    kViewInst,
    kViewAux,
    kViewBuses,
    kViewOutputs,
    kViewUser,
    kShift,
    kOption,
    kControl,
    kAlt,
    kAutoRead,
    kAutoWrite,
    kAutoTrim,
    kAutoTouch,
    kAutoLatch,
    kAutoGroup,
    kSave,
    kUndo,
    kCancel,
    kEnter,
    kMarker,
    kNudge,
    kCycle,
    kDrop,
    kReplace,
    kClick,
    kSolo,
    kRewind,
    kForward,
    kStop,
    kPlay,
    kRecord,
    kUp,
    kDown,
    kScrub,
    kZoom,
    kLeft,
    kRight,
  };

  // The lights with no button, which only the X-Touch has.
  enum class Led { kSmpte, kBeats, kSolo };

  // Faders 0-7 are on the strips. The master fader is only on the X-Touch.
  static constexpr int kMasterFader = 8;

  // The highest fader position: pitch bend's 14 bits.
  static constexpr int kFaderMax = 16383;

  //----------------------------------------------------------------------------
  // What the hardware shows
  //----------------------------------------------------------------------------

  enum class Light { kOff, kOn, kBlinking };

  // An encoder's light ring.
  struct Ring {
    bool operator==(const Ring&) const = default;

    // 0-3: a dot, boost/cut, wrap, or spread. 4-7: the same, with the far left
    // and right lights lit.
    int mode = 0;

    int position = 0;  // 0 for none lit, or 1-11.
  };

  // The scribble strip's colors.
  enum class ScribbleColor {
    kBlack,
    kRed,
    kGreen,
    kYellow,
    kBlue,
    kMagenta,
    kCyan,
    kWhite,
  };

  //----------------------------------------------------------------------------
  // Construction / Destruction
  //----------------------------------------------------------------------------

  // Lists the hardware's MIDI input and output ports in `reaper`, both named
  // `port_name`, and connects to the output.
  FakeXTouch(FakeReaper& reaper, Type type, std::string_view port_name);
  FakeXTouch(const FakeXTouch&) = delete;
  FakeXTouch& operator=(const FakeXTouch&) = delete;

  // Disconnects from the output.
  ~FakeXTouch();

  //----------------------------------------------------------------------------
  // Into JPRSurf
  //
  // Each is sent on the input port, at the fake's current time, and delivered
  // on the next MidiPorts::RunInput(). Nothing is sent unless the port is open
  // and started, as nothing is listening.
  //----------------------------------------------------------------------------

  void Press(Button button);
  void Press(StripButton button, int strip);
  void Release(Button button);
  void Release(StripButton button, int strip);

  // Letting go of a fader puts it back where it was last sent, as the hardware
  // does by itself (about a second later, where the fake does it at once).
  void TouchFader(int fader);
  void ReleaseFader(int fader);

  // Moves the fader by hand to `position`, from 0 to kFaderMax.
  void MoveFader(int fader, int position);

  // Turns the strip's encoder by `clicks`, from 1 to 63 either way, where
  // clockwise is positive.
  void TurnPot(int strip, int clicks);

  //----------------------------------------------------------------------------
  // What the hardware shows
  //
  // Everything starts off, at zero, blank, and black.
  //----------------------------------------------------------------------------

  Light GetLight(Button button) const;
  Light GetLight(StripButton button, int strip) const;
  Light GetLight(Led led) const;

  // The last position the fader was sent, or moved to by hand until it is
  // released.
  int GetFader(int fader) const;

  Ring GetRing(int strip) const;

  // The meter's level, 0x0-0xE. As on the hardware, a meter falls once a level
  // is sent, until another is. It falls one level for each run (1/30 second)
  // of the fake's clock: faster than the hardware, but close enough, and a
  // test sees at once that a meter wasn't sent again on the next run.
  int GetMeter(int strip) const;

  // The strip's 7 characters of the scribble strip's `line`, 0 or 1.
  std::string GetScribble(int strip, int line) const;

  ScribbleColor GetScribbleColor(int strip) const;

  // The timecode display's 10 characters, left to right, each with a '.' after
  // it if its dot is lit. Characters are as they were sent, whether or not
  // the display can draw them (it shows '@' blank, for instance).
  std::string GetTimecode() const;

  // The output port the hardware is connected to. A test that checks the raw
  // messages sent to the hardware records them there (see
  // FakeMidiOutput::SetRecording()).
  FakeMidiOutput* GetOutputPort() const { return output_; }

 private:
  // A meter's level, and when it was sent.
  struct Meter {
    int level = 0;
    double time = 0.0;
  };

  static constexpr int kStripCount = 8;
  static constexpr int kTimecodeDigits = 10;
  static constexpr int kScribbleLineLength = 56;
  static constexpr int kScribbleStripLength = 7;

  // Decodes a message sent to the hardware, failing the test if it can't.
  void Receive(absl::Span<const uint8_t> bytes);
  void ReceiveNote(absl::Span<const uint8_t> bytes);
  void ReceiveCc(absl::Span<const uint8_t> bytes);
  void ReceiveMeter(absl::Span<const uint8_t> bytes);
  void ReceiveFader(absl::Span<const uint8_t> bytes);
  void ReceiveSysex(absl::Span<const uint8_t> bytes);

  // Decode a sysex command's `data`, from the message `bytes`.
  void ReceiveScribbleText(absl::Span<const uint8_t> bytes,
                           absl::Span<const uint8_t> data);
  void ReceiveScribbleColors(absl::Span<const uint8_t> bytes,
                             absl::Span<const uint8_t> data);

  // Fails the test for a message the hardware can't take, saying why.
  void FailMessage(absl::Span<const uint8_t> bytes, std::string_view why) const;

  // Returns true if this model has the fader.
  bool HasFader(int fader) const;

  // Each returns true if this model has what is named, and fails the test if
  // it doesn't. CheckFull() checks for the X-Touch, naming `what` it has.
  bool CheckFull(std::string_view what) const;
  bool CheckStrip(int strip) const;
  bool CheckFader(int fader) const;

  // Sends a button or fader touch's note: pressed, or released.
  void SendNote(int note, bool pressed);

  const FakeReaper& reaper_;  // For the time, as meters fall.
  const Type type_;
  const std::string name_;
  FakeMidiInput* const input_;
  FakeMidiOutput* const output_;

  // Whether each note has a light on this model.
  std::array<bool, 128> has_light_ = {};

  // What the hardware shows.
  std::array<Light, 128> lights_ = {};  // By note.
  std::array<int, kStripCount + 1> faders_ = {};
  std::array<Ring, kStripCount> rings_ = {};
  std::array<Meter, kStripCount> meters_ = {};
  std::string scribble_;  // Both lines, one after the other.
  std::array<ScribbleColor, kStripCount> colors_ = {};
  std::array<uint8_t, kTimecodeDigits> timecode_ = {};  // Right to left.

  // Where each fader was last sent, which it goes back to when released.
  std::array<int, kStripCount + 1> sent_faders_ = {};
};

}  // namespace jpr
