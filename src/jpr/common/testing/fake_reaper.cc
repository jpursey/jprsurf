// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/fake_reaper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "jpr/common/guid.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/test_reset.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/test_control_surface.h"
#include "jpr/common/track_state.h"
#include "jpr/common/volume_utils.h"

namespace jpr {

namespace {

// The sizes of the buffers guidToString(), mkvolstr(), and mkpanstr() write.
constexpr int kTextSize = 64;

// The quietest volume mkvolstr() writes, below which it writes "-inf dB".
constexpr double kMinDecibels = -150.0;

// format_timestr_pos()'s modes that the fake writes.
constexpr int kTimeMode = 0;
constexpr int kBeatsMode = 2;
constexpr int kSamplesMode = 4;
constexpr int kFramesMode = 5;

// The project's time signature and rates, which are REAPER's defaults, as its
// tempo is (FakeProject::kBeatsPerMinute).
constexpr int kBeatsPerMeasure = 4;
constexpr int kFramesPerSecond = 30;
constexpr double kSamplesPerSecond = 44100.0;

}  // namespace

//==============================================================================
// FakeReaper::Api
//
// A static function for each function on the API list, with the same name and
// signature, which FakeReaper::GetFakeFunc() returns.
//==============================================================================

class FakeReaper::Api final {
 public:
  //----------------------------------------------------------------------------
  // Tracks
  //----------------------------------------------------------------------------

  static int CountTracks(ReaProject* project) {
    return s_instance_->FindProject(project).GetTrackCount();
  }

  static MediaTrack* GetTrack(ReaProject* project, int index) {
    FakeProject& fake_project = s_instance_->FindProject(project);
    if (index < 0 || index >= fake_project.GetTrackCount()) {
      return nullptr;
    }
    return ToMediaTrack(fake_project.GetTrack(index));
  }

  static MediaTrack* GetMasterTrack(ReaProject* project) {
    return ToMediaTrack(s_instance_->FindProject(project).GetMasterTrack());
  }

  static MediaTrack* GetParentTrack(MediaTrack* track_id) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    return ToMediaTrack(
        s_instance_->GetProjectOf(track).GetParentTrack(&track));
  }

  static GUID* GetTrackGUID(MediaTrack* track_id) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    FakeProject::TrackRecord* record =
        s_instance_->GetProjectOf(track).FindRecord(&track);
    if (record == nullptr) {
      // The scratch track GetTrack() returned has a zero GUID.
      s_instance_->unknown_guid_ = {};
      return &s_instance_->unknown_guid_;
    }
    // A record never moves, so its GUID stays put, as REAPER's does.
    return &record->guid;
  }

  static double GetMediaTrackInfo_Value(MediaTrack* track_id,
                                        const char* name) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    const std::string_view parameter(name);

    // The master's read as shown, even when it is hidden.
    if (parameter == "B_SHOWINMIXER") {
      return track.show_in_mixer || IsMaster(track) ? 1.0 : 0.0;
    }
    if (parameter == "B_SHOWINTCP") {
      return track.show_in_tcp || IsMaster(track) ? 1.0 : 0.0;
    }
    if (parameter == "I_AUTOMODE") {
      return track.auto_mode;
    }
    if (parameter == "IP_TRACKNUMBER") {
      // 1 for the first track, and -1 for the master.
      return IsMaster(track)
                 ? -1.0
                 : s_instance_->GetProjectOf(track).FindTrack(&track) + 1.0;
    }
    ADD_FAILURE() << "GetMediaTrackInfo_Value(\"" << parameter
                  << "\") isn't faked yet";
    return 0.0;
  }

  static bool GetSetMediaTrackInfo_String(MediaTrack* track_id,
                                          const char* name, char* value,
                                          bool set) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    if (std::string_view(name) == "P_NAME") {
      if (IsMaster(track)) {
        // The master's name can't be read or set, and reads as empty.
        if (!set) {
          value[0] = '\0';
        }
        return false;
      }
      if (set) {
        track.name = value;
      } else {
        // The API passes no size for `value`, so it must hold the name.
        track.name.copy(value, track.name.size());
        value[track.name.size()] = '\0';
      }
      return true;
    }
    ADD_FAILURE() << "GetSetMediaTrackInfo_String(\"" << name
                  << "\", set=" << set << ") isn't faked yet";
    return false;
  }

  static double Track_GetPeakInfo(MediaTrack* track_id, int channel) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    if (channel < 0 || channel >= static_cast<int>(track.peak.size())) {
      ADD_FAILURE() << "Track_GetPeakInfo() for channel " << channel
                    << " isn't faked yet";
      return 0.0;
    }
    return track.peak[channel];
  }

  static bool AnyTrackSolo(ReaProject* project) {
    return s_instance_->FindProject(project).AnyTrackSolo();
  }

  //----------------------------------------------------------------------------
  // Routes
  //
  // GetTrackNumSends() and GetSetTrackSendInfo() take a category: negative for
  // receives, 0 for sends, and positive for hardware outputs. The *TrackSendUI*
  // functions index a track's hardware outputs and then its sends from 0, and
  // its receives from -1 down, as -1 - index. REAPER only documents the
  // setters' receives, but the getters do the same. A hardware output's
  // P_SRCTRACK is its track, and its P_DESTTRACK is null.
  //----------------------------------------------------------------------------

  static int GetTrackNumSends(MediaTrack* track_id, int category) {
    return static_cast<int>(GetRoutes(track_id, category).size());
  }

  static void* GetSetTrackSendInfo(MediaTrack* track_id, int category,
                                   int index, const char* name, void* value) {
    const std::string_view parameter(name);
    if (value != nullptr ||
        (parameter != "P_DESTTRACK" && parameter != "P_SRCTRACK")) {
      ADD_FAILURE() << "GetSetTrackSendInfo(\"" << parameter
                    << "\", set=" << (value != nullptr) << ") isn't faked yet";
      return nullptr;
    }
    const FakeRoute* route = GetRouteAt(GetRoutes(track_id, category), index);
    if (route == nullptr) {
      return nullptr;
    }
    return ToMediaTrack(parameter == "P_DESTTRACK" ? route->destination
                                                   : route->source);
  }

  static bool GetTrackSendUIVolPan(MediaTrack* track_id, int index,
                                   double* volume, double* pan) {
    return GetRouteVolPan(GetTrackSendUiRoute(track_id, index), volume, pan);
  }

  static bool GetTrackReceiveUIVolPan(MediaTrack* track_id, int index,
                                      double* volume, double* pan) {
    return GetRouteVolPan(GetReceive(track_id, index), volume, pan);
  }

  static bool GetTrackSendUIMute(MediaTrack* track_id, int index, bool* mute) {
    return GetRouteMute(GetTrackSendUiRoute(track_id, index), mute);
  }

  static bool GetTrackReceiveUIMute(MediaTrack* track_id, int index,
                                    bool* mute) {
    return GetRouteMute(GetReceive(track_id, index), mute);
  }

  // Changes to routes aren't grouped. `end_edit` (REAPER's isend) is kEndEdit
  // to end an edit, which sets nothing, and otherwise 0, or -1 for an instant
  // edit. A pan past an end isn't clamped.
  static bool SetTrackSendUIVol(MediaTrack* track_id, int index, double volume,
                                int end_edit) {
    return SetRouteDouble(track_id, index, &FakeRoute::volume, volume,
                          end_edit);
  }

  static bool SetTrackSendUIPan(MediaTrack* track_id, int index, double pan,
                                int end_edit) {
    return SetRouteDouble(track_id, index, &FakeRoute::pan, pan, end_edit);
  }

  static bool ToggleTrackSendUIMute(MediaTrack* track_id, int index) {
    FakeRoute* route = GetTrackSendUiRoute(track_id, index);
    if (route == nullptr) {
      return false;
    }
    route->mute = !route->mute;
    return true;
  }

  //----------------------------------------------------------------------------
  // Track changes
  //
  // A change can also change other tracks, as REAPER's UI does. With selection
  // ganging, a change to a selected track changes every selected track, the
  // master too. Then with grouping, it changes every other track in a group
  // with one of those (see FakeTrack::group), but not the selected tracks of a
  // group's other tracks. SetTrackUI*()'s group flags allow both unless &1
  // ("prevent track grouping") or &2 ("prevent selection ganging") is set,
  // and CSurf_On*ChangeEx()'s allow_gang allows both.
  //
  // The other tracks get the same mute or solo, even if the track's didn't
  // change. A rec arm that changes the track sets its ganged tracks, but
  // toggles its grouped tracks, and one that doesn't change it changes
  // nothing. A volume or pan moves the others by the same change: volume by
  // its ratio, and pan by its difference. REAPER measures that change from
  // where each track was when the gesture began, which the fake doesn't
  // model, so a grouped change that clamps another track's pan, or moves a
  // volume from -inf, isn't faked yet.
  //----------------------------------------------------------------------------

  static void PreventUIRefresh(int count) {
    s_instance_->OnPreventUIRefresh(count);
  }

  // `mute` toggles the track's mute if negative, and otherwise sets it to
  // `mute > 0`. Returns the new mute.
  static int SetTrackUIMute(MediaTrack* track_id, int mute, int group_flags) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    s_instance_->OnBatchedChange(&track);
    const bool new_mute = GetNewValue(mute, track.mute);
    for (FakeTrack* changed : GetChangedTracks(track, group_flags).All()) {
      changed->mute = new_mute;
    }
    return new_mute ? 1 : 0;
  }

  // `solo` toggles the track's solo if negative, unsolos it if 0, and solos it
  // in place if 1 (REAPER's default) or 4, or not in place if 2. A toggle
  // solos it in place. Returns 0 for no solo, 1 for solo, or 2 for in place,
  // even for the master, whose solo is never in place.
  static int SetTrackUISolo(MediaTrack* track_id, int solo, int group_flags) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    s_instance_->OnBatchedChange(&track);
    if (solo == 3 || solo > 4) {
      ADD_FAILURE() << "SetTrackUISolo() with solo " << solo
                    << " isn't faked yet";
      return -1;
    }
    const bool new_solo = GetNewValue(solo, track.solo);
    const bool in_place = new_solo && solo != 2;
    const FakeTrack* master = s_instance_->GetProjectOf(track).GetMasterTrack();
    for (FakeTrack* changed : GetChangedTracks(track, group_flags).All()) {
      changed->solo = new_solo;
      changed->solo_in_place = in_place && changed != master;
    }
    return new_solo ? (in_place ? 2 : 1) : 0;
  }

  // `rec_arm` toggles the track's rec arm if negative, and otherwise sets it
  // to `rec_arm > 0`. Returns the new rec arm, or -1 for the master, which
  // can't be armed.
  static int SetTrackUIRecArm(MediaTrack* track_id, int rec_arm,
                              int group_flags) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    s_instance_->OnBatchedChange(&track);
    const FakeTrack* master = s_instance_->GetProjectOf(track).GetMasterTrack();
    if (&track == master) {
      return -1;
    }
    const bool new_rec_arm = GetNewValue(rec_arm, track.rec_arm);
    if (new_rec_arm == track.rec_arm) {
      return new_rec_arm ? 1 : 0;
    }
    const ChangedTracks changed = GetChangedTracks(track, group_flags);
    for (FakeTrack* ganged : changed.ganged) {
      if (ganged != master) {
        ganged->rec_arm = new_rec_arm;
      }
    }
    for (FakeTrack* grouped : changed.grouped) {
      grouped->rec_arm = !grouped->rec_arm;
    }
    return new_rec_arm ? 1 : 0;
  }

  static double CSurf_OnVolumeChangeEx(MediaTrack* track_id, double volume,
                                       bool relative, bool allow_gang) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    if (relative) {
      ADD_FAILURE() << "CSurf_OnVolumeChangeEx() with relative isn't faked yet";
      return track.volume;
    }
    const double old_volume = track.volume;
    for (FakeTrack* changed :
         GetChangedTracks(track, allow_gang ? 0 : kPreventGroupingAndGanging)
             .All()) {
      if (changed == &track) {
        changed->volume = volume;
      } else if (old_volume == 0.0) {
        ADD_FAILURE() << "A grouped volume change from -inf isn't faked yet";
      } else {
        changed->volume *= volume / old_volume;
      }
    }
    return volume;
  }

  // A pan past an end is clamped to it.
  static double CSurf_OnPanChangeEx(MediaTrack* track_id, double pan,
                                    bool relative, bool allow_gang) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    if (relative) {
      ADD_FAILURE() << "CSurf_OnPanChangeEx() with relative isn't faked yet";
      return track.pan;
    }
    const double new_pan = std::clamp(pan, -1.0, 1.0);
    const double change = new_pan - track.pan;
    for (FakeTrack* changed :
         GetChangedTracks(track, allow_gang ? 0 : kPreventGroupingAndGanging)
             .All()) {
      if (changed == &track) {
        changed->pan = new_pan;
        continue;
      }
      const double other_pan = changed->pan + change;
      if (other_pan < -1.0 || other_pan > 1.0) {
        ADD_FAILURE() << "A grouped pan change that clamps another track's "
                         "pan isn't faked yet";
      }
      changed->pan = std::clamp(other_pan, -1.0, 1.0);
    }
    return new_pan;
  }

  //----------------------------------------------------------------------------
  // Selection
  //----------------------------------------------------------------------------

  static void SetTrackSelected(MediaTrack* track_id, bool selected) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    s_instance_->OnBatchedChange(&track);
    track.selected = selected;
  }

  static void SetOnlyTrackSelected(MediaTrack* track_id) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    s_instance_->OnBatchedChange(&track);
    ReaProject* project = ToReaProject(&s_instance_->GetProjectOf(track));
    for (FakeTrack* other : GetSelectedTracks(project, /*want_master=*/true)) {
      other->selected = false;
    }
    track.selected = true;
  }

  static int CountSelectedTracks(ReaProject* project) {
    return static_cast<int>(
        GetSelectedTracks(project, /*want_master=*/false).size());
  }

  static int CountSelectedTracks2(ReaProject* project, bool want_master) {
    return static_cast<int>(GetSelectedTracks(project, want_master).size());
  }

  static MediaTrack* GetSelectedTrack(ReaProject* project, int index) {
    return GetSelectedTrack2(project, index, /*want_master=*/false);
  }

  static MediaTrack* GetSelectedTrack2(ReaProject* project, int index,
                                       bool want_master) {
    const std::vector<FakeTrack*> selected =
        GetSelectedTracks(project, want_master);
    if (index < 0 || index >= static_cast<int>(selected.size())) {
      return nullptr;
    }
    return ToMediaTrack(selected[index]);
  }

  //----------------------------------------------------------------------------
  // Undo
  //----------------------------------------------------------------------------

  static void Undo_OnStateChangeEx(const char* name, int flags,
                                   int track_parameter) {
    s_instance_->GetProject().undo_points_.push_back({name, flags});
  }

  // The fake has no FX or input monitoring, so their flags are never set.
  static const char* GetTrackState(MediaTrack* track_id, int* flags) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    *flags =
        (s_instance_->GetProjectOf(track).IsFolder(&track) ? kTrackStateFolder
                                                           : 0) |
        (track.selected ? kTrackStateSelected : 0) |
        (track.mute ? kTrackStateMute : 0) |
        (track.solo ? kTrackStateSolo : 0) |
        (track.solo && track.solo_in_place ? kTrackStateSoloInPlace : 0) |
        (track.rec_arm ? kTrackStateRecArm : 0) |
        (track.show_in_tcp ? 0 : kTrackStateHiddenInTcp) |
        (track.show_in_mixer ? 0 : kTrackStateHiddenInMixer);
    return track.name.c_str();
  }

  static bool GetTrackUIVolPan(MediaTrack* track_id, double* volume,
                               double* pan) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    *volume = track.volume;
    *pan = track.pan;
    return true;
  }

  // The master's color reads as none, even when it has one.
  static int GetTrackColor(MediaTrack* track_id) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    return IsMaster(track) ? 0 : track.color;
  }

  //----------------------------------------------------------------------------
  // MIDI ports
  //----------------------------------------------------------------------------

  static int GetNumMIDIInputs() {
    return static_cast<int>(s_instance_->midi_inputs_.size());
  }

  static int GetNumMIDIOutputs() {
    return static_cast<int>(s_instance_->midi_outputs_.size());
  }

  static bool GetMIDIInputName(int index, char* name, int name_size) {
    return GetMidiPortName(s_instance_->midi_inputs_, index, name, name_size);
  }

  static bool GetMIDIOutputName(int index, char* name, int name_size) {
    return GetMidiPortName(s_instance_->midi_outputs_, index, name, name_size);
  }

  static midi_Input* CreateMIDIInput(int index) {
    return OpenMidiPort(s_instance_->midi_inputs_, index, "input");
  }

  static midi_Output* CreateMIDIOutput(int index, bool stream_mode,
                                       int* ms_offset) {
    if (stream_mode) {
      ADD_FAILURE() << "CreateMIDIOutput() with stream mode isn't faked yet";
      return nullptr;
    }
    return OpenMidiPort(s_instance_->midi_outputs_, index, "output");
  }

  //----------------------------------------------------------------------------
  // Text and time
  //----------------------------------------------------------------------------

  static void guidToString(const GUID* guid, char* text) {
    absl::SNPrintF(text, kTextSize, "%s", FormatGuid(*guid));
  }

  // Reads a GUID in the format guidToString() writes.
  static void stringToGuid(const char* text, GUID* guid) {
    *guid = {};
    const std::string hex =
        absl::StrReplaceAll(text, {{"{", ""}, {"}", ""}, {"-", ""}});
    if (hex.size() == 32) {
      guid->Data1 = ParseHex(hex.substr(0, 8));
      guid->Data2 = static_cast<unsigned short>(ParseHex(hex.substr(8, 4)));
      guid->Data3 = static_cast<unsigned short>(ParseHex(hex.substr(12, 4)));
      for (int i = 0; i < 8; ++i) {
        guid->Data4[i] =
            static_cast<unsigned char>(ParseHex(hex.substr(16 + i * 2, 2)));
      }
    }
    // Anything that doesn't read back as it was written isn't a GUID.
    if (!absl::EqualsIgnoreCase(FormatGuid(*guid), text)) {
      ADD_FAILURE() << "stringToGuid() was given \"" << text
                    << "\", which isn't a GUID";
      *guid = {};
    }
  }

  // Writes a volume in decibels to three significant digits, as REAPER does:
  // "-14.2dB", "-5.10dB", "+4.23dB", or "-inf dB".
  static void mkvolstr(char* text, double volume) {
    const double decibels = VolumeToDecibels(volume);
    if (decibels < kMinDecibels) {
      absl::SNPrintF(text, kTextSize, "-inf dB");
      return;
    }
    const double magnitude = std::abs(decibels);
    const int decimals = magnitude >= 100.0 ? 0 : (magnitude >= 10.0 ? 1 : 2);
    absl::SNPrintF(text, kTextSize, "%+.*fdB", decimals, decibels);
  }

  // Writes a pan as REAPER does: "center", "25%L", or "100%R".
  static void mkpanstr(char* text, double pan) {
    const int percent = static_cast<int>(std::lround(std::abs(pan) * 100.0));
    if (percent == 0) {
      absl::SNPrintF(text, kTextSize, "center");
      return;
    }
    absl::SNPrintF(text, kTextSize, "%d%%%c", percent, pan < 0.0 ? 'L' : 'R');
  }

  // Writes a position in the modes JPRSurf reads, as REAPER does (see
  // FakeReaper's class comment for the project's tempo and rates). A negative
  // position is written as its distance from the start, after a "-".
  static void format_timestr_pos(double position, char* text, int text_size,
                                 int mode) {
    const char* sign = position < 0.0 ? "-" : "";
    const double seconds = std::abs(position);
    switch (mode) {
      case kTimeMode: {
        // Minutes:seconds.milliseconds, with hours if there are any.
        const int64_t total = std::llround(seconds * 1000.0);
        const int64_t hours = total / 3600000;
        const int64_t minutes = total / 60000 % 60;
        const int64_t whole_seconds = total / 1000 % 60;
        const int64_t milliseconds = total % 1000;
        if (hours > 0) {
          absl::SNPrintF(text, text_size, "%s%d:%02d:%02d.%03d", sign, hours,
                         minutes, whole_seconds, milliseconds);
        } else {
          absl::SNPrintF(text, text_size, "%s%d:%02d.%03d", sign, minutes,
                         whole_seconds, milliseconds);
        }
        return;
      }
      case kBeatsMode: {
        // Measure.beat.hundredths of a beat, from 1.1.00.
        const int64_t total =
            std::llround(seconds * FakeProject::kBeatsPerMinute / 60.0 * 100.0);
        const int64_t beats = total / 100;
        absl::SNPrintF(text, text_size, "%s%d.%d.%02d", sign,
                       beats / kBeatsPerMeasure + 1,
                       beats % kBeatsPerMeasure + 1, total % 100);
        return;
      }
      case kSamplesMode:
        absl::SNPrintF(text, text_size, "%s%d", sign,
                       std::llround(seconds * kSamplesPerSecond));
        return;
      case kFramesMode: {
        // Hours:minutes:seconds:frames.
        const int64_t total = std::llround(seconds * kFramesPerSecond);
        const int64_t total_seconds = total / kFramesPerSecond;
        absl::SNPrintF(text, text_size, "%s%02d:%02d:%02d:%02d", sign,
                       total_seconds / 3600, total_seconds / 60 % 60,
                       total_seconds % 60, total % kFramesPerSecond);
        return;
      }
    }
    ADD_FAILURE() << "format_timestr_pos() in mode " << mode
                  << " isn't faked yet";
    if (text_size > 0) {
      text[0] = '\0';
    }
  }

  static double time_precise() { return s_instance_->GetTime(); }

  static void ShowConsoleMsg(const char* message) {
    s_instance_->console_text_ += message;
  }

  //----------------------------------------------------------------------------
  // Transport
  //----------------------------------------------------------------------------

  static int GetPlayState() { return s_instance_->GetProject().play_state_; }

  static double GetPlayPosition() {
    return s_instance_->GetProject().play_position_;
  }

  static double GetCursorPosition() {
    return s_instance_->GetProject().cursor_position_;
  }

  //----------------------------------------------------------------------------
  // Actions
  //----------------------------------------------------------------------------

  static void Main_OnCommand(int command, int flag) {
    if (flag != 0) {
      ADD_FAILURE() << "Main_OnCommand() with flag " << flag
                    << " isn't faked yet";
    }
    s_instance_->commands_run_.push_back(command);
    FakeCommand* fake_command = FindCommand(command);
    if (fake_command != nullptr && fake_command->on_run) {
      fake_command->on_run();
    }
  }

  static int GetToggleCommandState(int command) {
    return s_instance_->GetToggleState(command);
  }

  static int NamedCommandLookup(const char* name) {
    for (const auto& [id, command] : s_instance_->commands_) {
      if (!command.name.empty() && command.name == name) {
        return id;
      }
    }
    return 0;
  }

  // REAPER has no text for an action it doesn't have.
  static const char* kbd_getTextFromCmd(int command, KbdSectionInfo* section) {
    if (section != nullptr) {
      ADD_FAILURE() << "kbd_getTextFromCmd() with a section isn't faked yet";
    }
    const FakeCommand* fake_command = FindCommand(command);
    return fake_command != nullptr ? fake_command->text.c_str() : "";
  }

  //----------------------------------------------------------------------------
  // Automation, undo, and the project
  //----------------------------------------------------------------------------

  static int GetGlobalAutomationOverride() {
    return s_instance_->GetProject().automation_override_;
  }

  static void SetGlobalAutomationOverride(int mode) {
    s_instance_->GetProject().automation_override_ = mode;
  }

  static const char* Undo_CanRedo2(ReaProject* project) {
    const FakeProject& fake_project = s_instance_->FindProject(project);
    return fake_project.redo_.empty() ? nullptr : fake_project.redo_.c_str();
  }

  static int IsProjectDirty(ReaProject* project) {
    return s_instance_->FindProject(project).dirty_ ? 1 : 0;
  }

  static int CountSelectedMediaItems(ReaProject* project) {
    return s_instance_->FindProject(project).selected_item_count_;
  }

 private:
  // The tracks a change to a track changes (see Track changes).
  struct ChangedTracks {
    // The track, and the selected tracks if it is ganged with them.
    std::vector<FakeTrack*> ganged;

    // The other tracks in a group with one of those.
    std::vector<FakeTrack*> grouped;

    std::vector<FakeTrack*> All() const {
      std::vector<FakeTrack*> tracks = ganged;
      tracks.insert(tracks.end(), grouped.begin(), grouped.end());
      return tracks;
    }
  };

  // Returns true if `track` is its project's master.
  static bool IsMaster(const FakeTrack& track) {
    return &track == s_instance_->GetProjectOf(track).GetMasterTrack();
  }

  // Returns the new value of a mute, solo, or rec arm set to `value`, which
  // toggles it if negative, and otherwise sets it to `value > 0`.
  static bool GetNewValue(int value, bool current) {
    return value < 0 ? !current : value > 0;
  }

  // Returns the tracks a change to `track` changes, with SetTrackUI*()'s
  // `group_flags`.
  static ChangedTracks GetChangedTracks(FakeTrack& track, int group_flags) {
    ChangedTracks changed;
    if (IsGanged(track, group_flags)) {
      changed.ganged = s_instance_->GetProjectOf(track).GetSelectedTracks(
          /*include_master=*/true);
    } else {
      changed.ganged = {&track};
    }
    if (IsGrouped(group_flags)) {
      changed.grouped = GetGroupedTracks(changed.ganged);
    }
    return changed;
  }

  // Returns every track in a group with one of `tracks`, that isn't one of
  // them.
  static std::vector<FakeTrack*> GetGroupedTracks(
      absl::Span<FakeTrack* const> tracks) {
    std::vector<FakeTrack*> grouped;
    if (std::ranges::none_of(tracks, &FakeTrack::group)) {
      return grouped;
    }
    const FakeProject& project = s_instance_->GetProjectOf(*tracks.front());
    for (int i = 0; i < project.GetTrackCount(); ++i) {
      FakeTrack* other = project.GetTrack(i);
      if (other->group != 0 &&
          std::ranges::find(tracks, other) == tracks.end() &&
          std::ranges::find(tracks, other->group, &FakeTrack::group) !=
              tracks.end()) {
        grouped.push_back(other);
      }
    }
    return grouped;
  }

  // Returns `track_id`'s routes in `category` (see Routes).
  static std::vector<FakeRoute*> GetRoutes(MediaTrack* track_id, int category) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    const FakeProject& project = s_instance_->GetProjectOf(track);
    if (category == kSendCategory) {
      return project.GetSends(&track);
    }
    const absl::Span<FakeRoute* const> routes =
        category < kSendCategory ? project.GetReceives(&track)
                                 : project.GetHardwareOutputs(&track);
    return {routes.begin(), routes.end()};
  }

  // Returns the route at `index` in `routes`, or null if there is none.
  static FakeRoute* GetRouteAt(absl::Span<FakeRoute* const> routes, int index) {
    if (index < 0 || index >= static_cast<int>(routes.size())) {
      return nullptr;
    }
    return routes[index];
  }

  // Returns `track_id`'s receive at `index`, or null if there is none.
  static FakeRoute* GetReceive(MediaTrack* track_id, int index) {
    return GetRouteAt(GetRoutes(track_id, kReceiveCategory), index);
  }

  // Returns `track_id`'s route at `index` as the *TrackSendUI* functions index
  // them (see Routes), or null if there is none.
  static FakeRoute* GetTrackSendUiRoute(MediaTrack* track_id, int index) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    return s_instance_->GetProjectOf(track).GetTrackSendUiRoute(&track, index);
  }

  // Reads `route`'s values, returning false if there is no route.
  static bool GetRouteVolPan(const FakeRoute* route, double* volume,
                             double* pan) {
    if (route == nullptr) {
      return false;
    }
    *volume = route->volume;
    *pan = route->pan;
    return true;
  }

  static bool GetRouteMute(const FakeRoute* route, bool* mute) {
    if (route == nullptr) {
      return false;
    }
    *mute = route->mute;
    return true;
  }

  // Sets the volume or pan of `track_id`'s route at `index` as the
  // *TrackSendUI* functions index them, unless `end_edit` is kEndEdit. Returns
  // false if there is none.
  static bool SetRouteDouble(MediaTrack* track_id, int index,
                             double FakeRoute::*property, double value,
                             int end_edit) {
    FakeRoute* route = GetTrackSendUiRoute(track_id, index);
    if (route == nullptr) {
      return false;
    }
    if (end_edit != kEndEdit) {
      route->*property = value;
    }
    return true;
  }

  // Copies the name of the MIDI port at `index` in `ports` to `name`, as
  // GetMIDIInputName() and GetMIDIOutputName() do, returning false if there is
  // none.
  template <typename Port>
  static bool GetMidiPortName(const std::vector<std::unique_ptr<Port>>& ports,
                              int index, char* name, int name_size) {
    if (index < 0 || index >= static_cast<int>(ports.size())) {
      return false;
    }
    absl::SNPrintF(name, name_size, "%s", ports[index]->GetName());
    return true;
  }

  // Opens the MIDI port at `index` in `ports`, as CreateMIDIInput() and
  // CreateMIDIOutput() do, returning null if there is none. A port that is
  // already open fails the test.
  template <typename Port>
  static Port* OpenMidiPort(const std::vector<std::unique_ptr<Port>>& ports,
                            int index, const char* kind) {
    if (index < 0 || index >= static_cast<int>(ports.size())) {
      return nullptr;
    }
    Port* port = ports[index].get();
    if (port->open_) {
      ADD_FAILURE() << "MIDI " << kind << " \"" << port->GetName()
                    << "\" was created while it is open";
      return nullptr;
    }
    port->open_ = true;
    return port;
  }

  // Returns the value of the hex digits `hex`, or 0 if they aren't any.
  static uint32_t ParseHex(std::string_view hex) {
    uint32_t value = 0;
    return absl::SimpleHexAtoi(hex, &value) ? value : 0;
  }

  // Returns the action `command`, or null if it wasn't added.
  static FakeCommand* FindCommand(int command) {
    auto it = s_instance_->commands_.find(command);
    return it != s_instance_->commands_.end() ? &it->second : nullptr;
  }

  // Returns the selected tracks in `project`, in order, with the master first
  // if `want_master` is true and it is selected.
  static std::vector<FakeTrack*> GetSelectedTracks(ReaProject* project,
                                                   bool want_master) {
    return s_instance_->FindProject(project).GetSelectedTracks(want_master);
  }
};

FakeReaper* FakeReaper::s_instance_ = nullptr;

FakeReaper::FakeReaper() {
  CHECK(s_instance_ == nullptr) << "Only one FakeReaper may exist at a time";
  s_instance_ = this;

  plugin_info_.caller_version = REAPER_PLUGIN_VERSION;
  plugin_info_.Register = &FakeReaper::Register;
  plugin_info_.GetFunc = &FakeReaper::GetFunc;

  AddProject();

  CHECK(LoadReaperApi(&FakeReaper::GetFakeFunc));
  MidiPorts::SetFlushWait(absl::ZeroDuration());
  TestReset::ResetAll();
}

FakeReaper::~FakeReaper() {
  EndEntryPoint();
  if (surface_ != nullptr) {
    ADD_FAILURE() << "A control surface is still open when FakeReaper is "
                     "destroyed";
    surface_->Close();
  }
  TestReset::ResetAll();

#define JPR_UNLOAD(name) ::name = nullptr;
  JPR_REAPER_API(JPR_UNLOAD)
#undef JPR_UNLOAD

  s_instance_ = nullptr;
}

//------------------------------------------------------------------------------
// The plugin and its control surface
//------------------------------------------------------------------------------

int FakeReaper::Register(const char* name, void* info) {
  if (std::string_view(name) != "csurf") {
    ADD_FAILURE() << "FakeReaper doesn't support Register(\"" << name << "\")";
    return 0;
  }
  s_instance_->surface_reg_ = static_cast<reaper_csurf_reg_t*>(info);
  return 1;
}

std::unique_ptr<TestControlSurface> FakeReaper::AddSurface(
    std::string_view config) {
  if (surface_reg_ == nullptr) {
    ADD_FAILURE() << "No control surface type is registered";
    return nullptr;
  }
  int errors = 0;
  IReaperControlSurface* surface = surface_reg_->create(
      surface_reg_->type_string, std::string(config).c_str(), &errors);
  EndEntryPoint();
  if (surface == nullptr) {
    return nullptr;
  }
  if (surface_ != nullptr) {
    ADD_FAILURE() << "A second control surface was created while one is open. "
                     "JPRSurf has one surface, with a listener for each use.";
    delete surface;
    EndEntryPoint();
    return nullptr;
  }
  auto test_surface = absl::WrapUnique(new TestControlSurface(this, surface));
  surface_ = test_surface.get();
  return test_surface;
}

void FakeReaper::RemoveSurface(TestControlSurface* surface) {
  if (surface == surface_) {
    surface_ = nullptr;
  }
}

//------------------------------------------------------------------------------
// Actions
//------------------------------------------------------------------------------

void FakeReaper::AddCommand(FakeCommand command) {
  const int id = command.id;
  if (!commands_.try_emplace(id, std::move(command)).second) {
    ADD_FAILURE() << "AddCommand() was given action " << id
                  << ", which was already added";
  }
}

void FakeReaper::SetToggleState(int id, int toggle_state) {
  auto it = commands_.find(id);
  if (it == commands_.end()) {
    ADD_FAILURE() << "SetToggleState() was given action " << id
                  << ", which wasn't added";
    return;
  }
  it->second.toggle_state = toggle_state;
}

int FakeReaper::GetToggleState(int id) const {
  auto it = commands_.find(id);
  return it != commands_.end() ? it->second.toggle_state : -1;
}

void FakeReaper::SetCommandHandler(int id, absl::AnyInvocable<void()> on_run) {
  auto it = commands_.find(id);
  if (it == commands_.end()) {
    ADD_FAILURE() << "SetCommandHandler() was given action " << id
                  << ", which wasn't added";
    return;
  }
  it->second.on_run = std::move(on_run);
}

//------------------------------------------------------------------------------
// MIDI ports
//------------------------------------------------------------------------------

FakeMidiInput* FakeReaper::AddMidiInput(std::string_view name) {
  return midi_inputs_.emplace_back(absl::WrapUnique(new FakeMidiInput(name)))
      .get();
}

FakeMidiOutput* FakeReaper::AddMidiOutput(std::string_view name) {
  return midi_outputs_.emplace_back(absl::WrapUnique(new FakeMidiOutput(name)))
      .get();
}

//------------------------------------------------------------------------------
// Time
//------------------------------------------------------------------------------

int FakeReaper::GetRunCount(absl::Duration duration) {
  // In integers, so a whole number of seconds is exact.
  constexpr int64_t kNanosecondsPerSecond = 1'000'000'000;
  return static_cast<int>((absl::ToInt64Nanoseconds(duration) * kRunsPerSecond +
                           kNanosecondsPerSecond - 1) /
                          kNanosecondsPerSecond);
}

double FakeReaper::GetTime() const {
  // REAPER's clock doesn't start at zero either.
  constexpr double kStartTime = 1000.0;
  return kStartTime + static_cast<double>(run_count_) / kRunsPerSecond +
         absl::ToDoubleSeconds(advanced_time_);
}

//------------------------------------------------------------------------------
// Projects
//------------------------------------------------------------------------------

FakeProject& FakeReaper::AddProject() {
  open_projects_.push_back(CreateProject());
  current_project_ = GetProjectCount() - 1;
  return GetProject();
}

void FakeReaper::SwitchProjectTo(int index) {
  if (index < 0 || index >= GetProjectCount()) {
    ADD_FAILURE() << "SwitchProjectTo(" << index << ") with only "
                  << GetProjectCount() << " project(s) open";
    return;
  }
  current_project_ = index;
}

FakeProject& FakeReaper::NewProject() {
  open_projects_[current_project_] = CreateProject();
  return GetProject();
}

FakeProject* FakeReaper::CreateProject() {
  const int number = static_cast<int>(projects_.size()) + 1;
  projects_.push_back(absl::WrapUnique(new FakeProject(number)));
  return projects_.back().get();
}

FakeProject& FakeReaper::FindProject(ReaProject* project) {
  if (project == nullptr) {
    return GetProject();
  }
  // The pointer is only compared, until it is known to be a project.
  auto it = std::ranges::find(open_projects_,
                              reinterpret_cast<FakeProject*>(project));
  if (it == open_projects_.end()) {
    ADD_FAILURE() << "ReaProject " << project
                  << " isn't an open project in FakeReaper";
    return GetProject();
  }
  return **it;
}

FakeTrack& FakeReaper::GetTrack(MediaTrack* track_id) {
  // The pointer is only compared, until it is known to be a track.
  FakeTrack* track = reinterpret_cast<FakeTrack*>(track_id);
  if (FindOpenProject(track) != nullptr) {
    return *track;
  }
  if (std::ranges::any_of(projects_, [track](const auto& project) {
        return project->HadTrack(track);
      })) {
    ADD_FAILURE() << "MediaTrack " << track_id << " is a deleted track";
  } else if (std::ranges::any_of(projects_, [track](const auto& project) {
               return project->HasTrack(track);
             })) {
    ADD_FAILURE() << "MediaTrack " << track_id
                  << " is a track in a project that is no longer open";
  } else {
    ADD_FAILURE() << "MediaTrack " << track_id
                  << " isn't a track in FakeReaper";
  }
  unknown_track_ = {};
  return unknown_track_;
}

FakeProject& FakeReaper::GetProjectOf(const FakeTrack& track) {
  FakeProject* project = FindOpenProject(&track);
  return project != nullptr ? *project : GetProject();
}

FakeProject* FakeReaper::FindOpenProject(const FakeTrack* track) {
  auto it = std::ranges::find_if(
      open_projects_,
      [track](const FakeProject* project) { return project->HasTrack(track); });
  return it != open_projects_.end() ? *it : nullptr;
}

//------------------------------------------------------------------------------
// Checks
//------------------------------------------------------------------------------

void FakeReaper::OnPreventUIRefresh(int count) {
  batch_depth_ += count;
  if (batch_depth_ < 0) {
    ADD_FAILURE() << "PreventUIRefresh(" << count
                  << ") ended a scope that wasn't started";
    batch_depth_ = 0;
  }
  if (batch_depth_ == 0) {
    EndBatch();
  }
}

void FakeReaper::EndBatch() {
  if (changed_in_batch_) {
    changed_in_batch_ = false;
    ++change_count_;
  }
}

void FakeReaper::OnBatchedChange(const FakeTrack* track) {
  changed_tracks_.insert(track);
  if (batch_depth_ > 0) {
    changed_in_batch_ = true;
  } else {
    ++change_count_;
  }
}

void FakeReaper::EndEntryPoint() {
  if (batch_depth_ != 0) {
    ADD_FAILURE() << "PreventUIRefresh() is unbalanced by " << batch_depth_
                  << " at the end of an entry point";
    batch_depth_ = 0;
    EndBatch();
  }
  if (change_count_ > 1 && changed_tracks_.size() > 1) {
    ADD_FAILURE() << "Mute, solo, rec arm, or selection changed on "
                  << changed_tracks_.size() << " tracks, in " << change_count_
                  << " separate changes, in one entry point. Change several "
                     "tracks in one TrackBatch, for one UI refresh and one "
                     "undo point.";
  }
  change_count_ = 0;
  changed_tracks_.clear();
}

void* FakeReaper::GetFakeFunc(const char* name) {
  // Every function on the list must have a member of Api with the same name
  // and signature, or this doesn't compile.
  const std::string_view requested(name);
#define JPR_FAKE_FUNCTION(name)                                               \
  if (requested == #name) {                                                   \
    return reinterpret_cast<void*>(static_cast<decltype(::name)>(Api::name)); \
  }
  JPR_REAPER_API(JPR_FAKE_FUNCTION)
#undef JPR_FAKE_FUNCTION
  return nullptr;
}

void* FakeReaper::GetFunc(const char* name) {
  // The leading :: is needed, as FakeReaper has members with some of the same
  // names, such as GetTrack().
  const std::string_view requested(name);
#define JPR_LOADED_FUNCTION(name)           \
  if (requested == #name) {                 \
    return reinterpret_cast<void*>(::name); \
  }
  JPR_REAPER_API(JPR_LOADED_FUNCTION)
#undef JPR_LOADED_FUNCTION
  return nullptr;
}

}  // namespace jpr
