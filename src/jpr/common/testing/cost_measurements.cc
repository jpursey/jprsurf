// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/cost_measurements.h"

#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "jpr/common/action_ids.h"
#include "jpr/common/automation.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/cost_measurement.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/large_project.h"
#include "jpr/common/testing/reaper_action_list.h"
#include "jpr/common/timeline.h"
#include "jpr/common/track_state.h"

namespace jpr {
namespace {

// The large project's folders for each size measured: 128 tracks, and a
// smaller and a larger project, for what may scale with the project.
constexpr int kFolders = 12;
constexpr int kFewFolders = 2;    // 28 tracks.
constexpr int kManyFolders = 50;  // 508 tracks.

// What a change acts on: a track in a folder with one send (T6.2), the track
// after it, and a bus, with a receive from every fourth track (T1).
constexpr int kTrack = 20;
constexpr int kOtherTrack = 21;
constexpr int kBus = 0;

// How many times a read that reads no track is repeated in a sample.
constexpr int kRepeats = 100;

// The current project's tracks, which each measurement finds once its project
// is open (FindTracks()), as only one runs at a time.
std::vector<MediaTrack*> g_tracks;

void FindTracks() {
  g_tracks.clear();
  const int count = CountTracks(nullptr);
  for (int i = 0; i < count; ++i) {
    g_tracks.push_back(GetTrack(nullptr, i));
  }
}

// Which tracks a project starts with selected.
enum class Selection {
  kOneTrack,    // kTrack.
  kEveryTrack,  // Every track but the master.
};

// Returns a measurement of `name` on the large project with `folders`, which
// finds its tracks once the project is open.
CostMeasurement OnProject(std::string name, int folders = kFolders,
                          Selection selection = Selection::kOneTrack) {
  std::string condition =
      absl::StrCat(GetLargeProjectTrackCount(folders), " tracks");
  if (selection == Selection::kEveryTrack) {
    absl::StrAppend(&condition, ", every track selected");
  }
  return {.name = std::move(name),
          .condition = std::move(condition),
          .build =
              [folders, selection](FakeProject& project) {
                AddLargeProject(&project, folders);
                for (int i = 0; i < project.GetTrackCount(); ++i) {
                  project.GetTrack(i)->selected =
                      selection == Selection::kEveryTrack || i == kTrack;
                }
                project.SetSelectedItemCount(1);
                project.SetCursorPosition(60.0);
              },
          .prepare = &FindTracks};
}

//------------------------------------------------------------------------------
// Reads
//------------------------------------------------------------------------------

using Read = void (*)();
using TrackRead = void (*)(MediaTrack* track, int index);

struct NamedRead {
  const char* name;
  Read read;
};

struct NamedTrackRead {
  const char* name;
  TrackRead read;
};

// Reads `read` kRepeats times.
CostMeasurement ReadRepeated(std::string name, Read read,
                             int folders = kFolders,
                             Selection selection = Selection::kOneTrack) {
  CostMeasurement measurement = OnProject(std::move(name), folders, selection);
  measurement.read = [read] {
    for (int i = 0; i < kRepeats; ++i) {
      read();
    }
    return kRepeats;
  };
  return measurement;
}

// Reads `read` on each track, with its index, in turn.
CostMeasurement ReadEachTrack(std::string name, TrackRead read,
                              int folders = kFolders,
                              Selection selection = Selection::kOneTrack) {
  CostMeasurement measurement = OnProject(std::move(name), folders, selection);
  measurement.read = [read] {
    const int count = static_cast<int>(g_tracks.size());
    for (int i = 0; i < count; ++i) {
      read(g_tracks[i], i);
    }
    return count;
  };
  return measurement;
}

// Reads `read` on the bus, with each of its receives' index in turn.
CostMeasurement ReadEachReceive(std::string name, TrackRead read) {
  CostMeasurement measurement = OnProject(std::move(name));
  measurement.read = [read] {
    constexpr int kCount = GetLargeProjectBusReceiveCount(kFolders);
    MediaTrack* const bus = g_tracks[kBus];
    for (int i = 0; i < kCount; ++i) {
      read(bus, i);
    }
    return kCount;
  };
  return measurement;
}

// Reads `read` on each track but the buses, with its first send's index.
CostMeasurement ReadEachSend(std::string name, TrackRead read) {
  CostMeasurement measurement = OnProject(std::move(name));
  measurement.read = [read] {
    const int count = static_cast<int>(g_tracks.size());
    for (int i = kLargeProjectBusCount; i < count; ++i) {
      read(g_tracks[i], 0);
    }
    return count - kLargeProjectBusCount;
  };
  return measurement;
}

// What may scale with the project, at each size.
constexpr NamedRead kScaledReads[] = {
    {"CountTracks", [] { CountTracks(nullptr); }},
    {"AnyTrackSolo", [] { AnyTrackSolo(nullptr); }},
    {"CountSelectedMediaItems", [] { CountSelectedMediaItems(nullptr); }},
};
constexpr NamedTrackRead kScaledTrackReads[] = {
    {"GetTrack",
     [](MediaTrack* track, int index) { GetTrack(nullptr, index); }},
    {"GetTrackState",
     [](MediaTrack* track, int index) {
       int flags = 0;
       GetTrackState(track, &flags);
     }},
};

constexpr NamedTrackRead kTrackReads[] = {
    {"GetMediaTrackInfo_Value",
     [](MediaTrack* track, int index) {
       GetMediaTrackInfo_Value(track, "B_SHOWINTCP");
     }},
    {"GetSetMediaTrackInfo_String",
     [](MediaTrack* track, int index) {
       char name[256];
       GetSetMediaTrackInfo_String(track, "P_NAME", name,
                                   /*setNewValue=*/false);
     }},
    {"GetTrackColor",
     [](MediaTrack* track, int index) { GetTrackColor(track); }},
    {"GetTrackGUID", [](MediaTrack* track, int index) { GetTrackGUID(track); }},
    {"GetTrackUIVolPan",
     [](MediaTrack* track, int index) {
       double volume = 0;
       double pan = 0;
       GetTrackUIVolPan(track, &volume, &pan);
     }},
    {"GetParentTrack",
     [](MediaTrack* track, int index) { GetParentTrack(track); }},
    {"GetTrackNumSends",
     [](MediaTrack* track, int index) {
       GetTrackNumSends(track, kSendCategory);
     }},
    {"Track_GetPeakInfo",
     [](MediaTrack* track, int index) { Track_GetPeakInfo(track, 0); }},
};

constexpr NamedTrackRead kReceiveReads[] = {
    {"GetSetTrackSendInfo",
     [](MediaTrack* track, int index) {
       GetSetTrackSendInfo(track, kReceiveCategory, index, "P_SRCTRACK",
                           nullptr);
     }},
    {"GetTrackReceiveUIVolPan",
     [](MediaTrack* track, int index) {
       double volume = 0;
       double pan = 0;
       GetTrackReceiveUIVolPan(track, index, &volume, &pan);
     }},
    {"GetTrackReceiveUIMute",
     [](MediaTrack* track, int index) {
       bool mute = false;
       GetTrackReceiveUIMute(track, index, &mute);
     }},
};

constexpr NamedTrackRead kSendReads[] = {
    {"GetTrackSendUIVolPan",
     [](MediaTrack* track, int index) {
       double volume = 0;
       double pan = 0;
       GetTrackSendUIVolPan(track, index, &volume, &pan);
     }},
    {"GetTrackSendUIMute",
     [](MediaTrack* track, int index) {
       bool mute = false;
       GetTrackSendUIMute(track, index, &mute);
     }},
};

// What depends on how many tracks are selected, with one, and every one.
constexpr NamedRead kSelectionReads[] = {
    {"CountSelectedTracks", [] { CountSelectedTracks(nullptr); }},
    {"CountSelectedTracks2",
     [] { CountSelectedTracks2(nullptr, /*wantmaster=*/true); }},
};

// With every track selected.
constexpr NamedTrackRead kSelectedTrackReads[] = {
    {"GetSelectedTrack",
     [](MediaTrack* track, int index) { GetSelectedTrack(nullptr, index); }},
    {"GetSelectedTrack2",
     [](MediaTrack* track, int index) {
       GetSelectedTrack2(nullptr, index, /*wantmaster=*/true);
     }},
};

constexpr NamedRead kRepeatedReads[] = {
    // The project, transport, and actions.
    {"EnumProjects", [] { EnumProjects(-1, nullptr, 0); }},
    {"GetCursorPosition", [] { GetCursorPosition(); }},
    {"GetGlobalAutomationOverride", [] { GetGlobalAutomationOverride(); }},
    {"GetMasterTrack", [] { GetMasterTrack(nullptr); }},
    {"GetPlayPosition", [] { GetPlayPosition(); }},
    {"GetPlayState", [] { GetPlayState(); }},
    {"IsProjectDirty", [] { IsProjectDirty(nullptr); }},
    {"Undo_CanRedo2", [] { Undo_CanRedo2(nullptr); }},
    {"GetToggleCommandState", [] { GetToggleCommandState(kRepeatAction); }},
    {"kbd_getTextFromCmd", [] { kbd_getTextFromCmd(kUndoAction, nullptr); }},
    {"NamedCommandLookup", [] { NamedCommandLookup("40029"); }},
    {"time_precise", [] { time_precise(); }},

    // Text.
    {"format_timestr_pos",
     [] {
       char text[64];
       format_timestr_pos(12.345, text, static_cast<int>(std::size(text)),
                          kFormatBeats);
     }},
    {"mkvolstr",
     [] {
       char text[64];
       mkvolstr(text, 0.5);
     }},
    {"mkpanstr",
     [] {
       char text[64];
       mkpanstr(text, 0.25);
     }},
    {"guidToString",
     [] {
       GUID guid = {};
       char text[64];
       guidToString(&guid, text);
     }},
    {"stringToGuid",
     [] {
       GUID guid = {};
       stringToGuid("{B2C3D4E5-F6A7-4B8C-9D0E-1F2A3B4C5D6E}", &guid);
     }},
};

void AddReads(std::vector<CostMeasurement>* measurements) {
  for (int folders : {kFewFolders, kFolders, kManyFolders}) {
    for (const NamedRead& read : kScaledReads) {
      measurements->push_back(ReadRepeated(read.name, read.read, folders));
    }
    for (const NamedTrackRead& read : kScaledTrackReads) {
      measurements->push_back(ReadEachTrack(read.name, read.read, folders));
    }
  }
  for (const NamedTrackRead& read : kTrackReads) {
    measurements->push_back(ReadEachTrack(read.name, read.read));
  }
  for (const NamedTrackRead& read : kReceiveReads) {
    measurements->push_back(ReadEachReceive(read.name, read.read));
  }
  for (const NamedTrackRead& read : kSendReads) {
    measurements->push_back(ReadEachSend(read.name, read.read));
  }
  for (Selection selection : {Selection::kOneTrack, Selection::kEveryTrack}) {
    for (const NamedRead& read : kSelectionReads) {
      measurements->push_back(
          ReadRepeated(read.name, read.read, kFolders, selection));
    }
  }
  for (const NamedTrackRead& read : kSelectedTrackReads) {
    measurements->push_back(
        ReadEachTrack(read.name, read.read, kFolders, Selection::kEveryTrack));
  }
  for (const NamedRead& read : kRepeatedReads) {
    measurements->push_back(ReadRepeated(read.name, read.read));
  }
}

//------------------------------------------------------------------------------
// Changes
//
// The extension only changes REAPER inside a run, which is a deferred refresh
// (see DeferredRefresh), so changes are measured in one, where the end of the
// deferred refresh shows what a setter put off. Only a mute and a send's
// volume are also measured outside one, to compare.
//------------------------------------------------------------------------------

// Makes a change, which `on` alternates from true to false.
using ChangeCall = void (*)(bool on);

// Changes the project with `change`.
CostMeasurement Change(std::string name, ChangeCall change,
                       int folders = kFolders,
                       Selection selection = Selection::kOneTrack,
                       bool in_deferred_refresh = true) {
  CostMeasurement measurement = OnProject(std::move(name), folders, selection);
  measurement.in_deferred_refresh = in_deferred_refresh;
  measurement.call = [change, on = false]() mutable {
    on = !on;
    change(on);
  };
  return measurement;
}

void MuteTrack(bool on) {
  SetTrackUIMute(g_tracks[kTrack], on ? 1 : 0, kPreventGroupingAndGanging);
}
void RecArmTrack(bool on) {
  SetTrackUIRecArm(g_tracks[kTrack], on ? 1 : 0, kPreventGroupingAndGanging);
}
void SelectOtherTrack(bool on) { SetTrackSelected(g_tracks[kOtherTrack], on); }

// Toggles kTrack's mute, which the next undo point holds.
void ToggleMute() {
  SetTrackUIMute(g_tracks[kTrack], -1, kPreventGroupingAndGanging);
}

// Toggles kTrack's mute, and adds an undo point for it, so Undo and Redo, and
// the next undo point, have something to undo.
void AddMuteUndoPoint() {
  ToggleMute();
  Undo_OnStateChangeEx("Measure", UNDO_STATE_TRACKCFG, -1);
}

// Adds an undo point with `change`, which holds a mute made before it.
CostMeasurement UndoPoint(std::string name, ChangeCall change,
                          int folders = kFolders) {
  CostMeasurement measurement = Change(std::move(name), change, folders);
  measurement.prepare = [] {
    FindTracks();
    ToggleMute();
  };
  measurement.reset = &ToggleMute;
  return measurement;
}

void AddUndoPoint(bool on) {
  Undo_OnStateChangeEx("Measure", UNDO_STATE_TRACKCFG, -1);
}

// Each changes every track in turn.
void MuteEveryTrack(bool on) {
  for (MediaTrack* track : g_tracks) {
    SetTrackUIMute(track, on ? 1 : 0, kPreventGroupingAndGanging);
  }
}
void RecArmEveryTrack(bool on) {
  for (MediaTrack* track : g_tracks) {
    SetTrackUIRecArm(track, on ? 1 : 0, kPreventGroupingAndGanging);
  }
}
void SelectEveryTrack(bool on) {
  for (MediaTrack* track : g_tracks) {
    SetTrackSelected(track, on);
  }
}

// Changes every track with `change`, in one deferred refresh, as a ranged
// gesture changes them.
CostMeasurement ChangeEveryTrack(std::string name, ChangeCall change) {
  CostMeasurement measurement = Change(std::move(name), change);
  measurement.condition =
      absl::StrCat(measurement.condition, ", every track changed");
  return measurement;
}

// Changes a route of kTrack (its send, index 0) and of the bus (its first
// receive, index -1), as an edit in progress, as the extension changes them.
void AddRouteChanges(std::vector<CostMeasurement>* measurements,
                     std::string name, ChangeCall send_change,
                     ChangeCall receive_change) {
  measurements->push_back(Change(name, send_change));
  CostMeasurement receive = Change(std::move(name), receive_change);
  receive.condition = absl::StrCat(receive.condition, ", a bus's receive");
  measurements->push_back(std::move(receive));
}

void AddChanges(std::vector<CostMeasurement>* measurements) {
  // Tracks.
  measurements->push_back(Change("SetTrackUIMute", &MuteTrack));
  measurements->push_back(Change("SetTrackUISolo", [](bool on) {
    SetTrackUISolo(g_tracks[kTrack], on ? 1 : 0, kPreventGroupingAndGanging);
  }));
  measurements->push_back(Change("SetTrackUIRecArm", &RecArmTrack));
  measurements->push_back(Change("SetTrackSelected", &SelectOtherTrack));
  measurements->push_back(Change("SetOnlyTrackSelected", [](bool on) {
    SetOnlyTrackSelected(g_tracks[on ? kOtherTrack : kTrack]);
  }));
  measurements->push_back(Change("CSurf_OnVolumeChangeEx", [](bool on) {
    CSurf_OnVolumeChangeEx(g_tracks[kTrack], on ? 0.5 : 1.0,
                           /*relative=*/false, /*allowGang=*/false);
  }));
  measurements->push_back(Change("CSurf_OnPanChangeEx", [](bool on) {
    CSurf_OnPanChangeEx(g_tracks[kTrack], on ? 0.5 : 0.0, /*relative=*/false,
                        /*allowGang=*/false);
  }));
  measurements->push_back(Change("GetSetMediaTrackInfo_String", [](bool on) {
    char name[] = "Renamed";
    char other_name[] = "Measured";
    GetSetMediaTrackInfo_String(g_tracks[kTrack], "P_NAME",
                                on ? name : other_name, /*setNewValue=*/true);
  }));
  measurements->push_back(Change("SetGlobalAutomationOverride", [](bool on) {
    SetGlobalAutomationOverride(
        static_cast<int>(on ? AutoOverride::kRead : AutoOverride::kNone));
  }));
  measurements->push_back(Change("SetTrackUIMute", &MuteTrack, kFolders,
                                 Selection::kOneTrack,
                                 /*in_deferred_refresh=*/false));

  // How changes scale with the project.
  for (int folders : {kFewFolders, kManyFolders}) {
    measurements->push_back(Change("SetTrackUIMute", &MuteTrack, folders));
    measurements->push_back(Change("SetTrackUIRecArm", &RecArmTrack, folders));
    measurements->push_back(
        Change("SetTrackSelected", &SelectOtherTrack, folders));
    measurements->push_back(
        UndoPoint("Undo_OnStateChangeEx", &AddUndoPoint, folders));
  }

  // Every track in one deferred refresh.
  measurements->push_back(ChangeEveryTrack("SetTrackUIMute", &MuteEveryTrack));
  measurements->push_back(
      ChangeEveryTrack("SetTrackUIRecArm", &RecArmEveryTrack));
  measurements->push_back(
      ChangeEveryTrack("SetTrackSelected", &SelectEveryTrack));

  // Ganged across every selected track, as the extension's grouped changes
  // are.
  measurements->push_back(Change(
      "SetTrackUIMute",
      [](bool on) { SetTrackUIMute(g_tracks[kTrack], on ? 1 : 0, 0); },
      kFolders, Selection::kEveryTrack));
  measurements->push_back(Change(
      "CSurf_OnVolumeChangeEx",
      [](bool on) {
        CSurf_OnVolumeChangeEx(g_tracks[kTrack], on ? 0.5 : 1.0,
                               /*relative=*/false, /*allowGang=*/true);
      },
      kFolders, Selection::kEveryTrack));

  // Routes.
  AddRouteChanges(
      measurements, "SetTrackSendUIVol",
      [](bool on) {
        SetTrackSendUIVol(g_tracks[kTrack], 0, on ? 0.5 : 1.0, /*isend=*/0);
      },
      [](bool on) {
        SetTrackSendUIVol(g_tracks[kBus], -1, on ? 0.5 : 1.0, /*isend=*/0);
      });
  AddRouteChanges(
      measurements, "SetTrackSendUIPan",
      [](bool on) {
        SetTrackSendUIPan(g_tracks[kTrack], 0, on ? 0.5 : 0.0, /*isend=*/0);
      },
      [](bool on) {
        SetTrackSendUIPan(g_tracks[kBus], -1, on ? 0.5 : 0.0, /*isend=*/0);
      });
  AddRouteChanges(
      measurements, "ToggleTrackSendUIMute",
      [](bool on) { ToggleTrackSendUIMute(g_tracks[kTrack], 0); },
      [](bool on) { ToggleTrackSendUIMute(g_tracks[kBus], -1); });
  measurements->push_back(Change(
      "SetTrackSendUIVol",
      [](bool on) {
        SetTrackSendUIVol(g_tracks[kTrack], 0, on ? 0.5 : 1.0, /*isend=*/0);
      },
      kFolders, Selection::kOneTrack, /*in_deferred_refresh=*/false));

  // A deferred refresh with nothing in it, whose end is timed.
  CostMeasurement empty = Change("PreventUIRefresh", [](bool on) {});
  empty.condition = absl::StrCat(empty.condition, ", nothing changed");
  measurements->push_back(std::move(empty));

  // Undo points.
  measurements->push_back(UndoPoint("Undo_OnStateChangeEx", &AddUndoPoint));
  measurements->push_back(UndoPoint("Undo_OnStateChangeEx2", [](bool on) {
    Undo_OnStateChangeEx2(nullptr, "Measure", UNDO_STATE_TRACKCFG, -1);
  }));
}

//------------------------------------------------------------------------------
// Actions
//------------------------------------------------------------------------------

// What an action needs to run again and again: what it needs first, and what
// resets it after each run (see CostMeasurement::reset).
struct ActionSetup {
  void (*prepare)() = nullptr;
  void (*reset)() = nullptr;
};

void Play() { Main_OnCommand(kPlayAction, 0); }
void Stop() { Main_OnCommand(kStopAction, 0); }
void Undo() { Main_OnCommand(kUndoAction, 0); }
void GoToNextMarker() { Main_OnCommand(kNextMarkerAction, 0); }
void GoToPreviousMarker() { Main_OnCommand(kPreviousMarkerAction, 0); }
void SetTrimRead() {
  Main_OnCommand(GetAutoModeAction(AutoMode::kTrimRead), 0);
}
void SetRead() { Main_OnCommand(GetAutoModeAction(AutoMode::kRead), 0); }
void Solo() { SetTrackUISolo(g_tracks[kTrack], 1, kPreventGroupingAndGanging); }

ActionSetup GetActionSetup(int action) {
  switch (action) {
    case kPlayAction:
      return {.reset = &Stop};
    case kStopAction:
      return {.prepare = &Play, .reset = &Play};
    case kUndoAction:
      return {.prepare = &AddMuteUndoPoint, .reset = &AddMuteUndoPoint};
    case kRedoAction:
      return {.prepare =
                  [] {
                    AddMuteUndoPoint();
                    Undo();
                  },
              .reset = &Undo};
    case kInsertClickAction:
    case kInsertItemAction:
    case kInsertMidiItemAction:
      return {.reset = &Undo};
    case kNextMarkerAction:
      return {.reset = &GoToPreviousMarker};
    case kPreviousMarkerAction:
      return {.prepare = &GoToNextMarker, .reset = &GoToNextMarker};
    case kUnsoloAllAction:
      return {.prepare = &Solo, .reset = &Solo};
    case GetAutoModeAction(AutoMode::kTrimRead):
      return {.reset = &SetRead};
    case GetAutoModeAction(AutoMode::kRead):
    case GetAutoModeAction(AutoMode::kTouch):
    case GetAutoModeAction(AutoMode::kWrite):
    case GetAutoModeAction(AutoMode::kLatch):
    case GetAutoModeAction(AutoMode::kLatchPreview):
      return {.reset = &SetTrimRead};
  }
  return {};
}

// The ruler's time unit as the ruler measurement starts, which it restores
// after each call, as REAPER keeps it as a preference, and the one it changes
// to.
int g_ruler_mode = kRulerMeasuresBeats;
int g_other_ruler_mode = kRulerSeconds;

void FindRulerModes() {
  for (const RulerMode& mode : kRulerModes) {
    if (GetToggleCommandState(mode.id) > 0) {
      g_ruler_mode = mode.id;
    }
  }
  g_other_ruler_mode =
      g_ruler_mode == kRulerSeconds ? kRulerMeasuresBeats : kRulerSeconds;
}

// Adds a measurement of each action, on the large project, where kTrack is
// selected, a media item is selected, and the edit cursor is a minute in (see
// OnProject()), so each action has something to act on.
void AddActions(std::vector<CostMeasurement>* measurements) {
  for (const ReaperAction& action : kReaperActions) {
    // Recording writes media files, and stopping it may ask what to do with
    // them.
    if (action.id == kRecordAction) {
      continue;
    }
    CostMeasurement measurement = OnProject("Main_OnCommand");
    measurement.condition =
        absl::StrCat(action.text, ", ", measurement.condition);
    measurement.call = [id = action.id] { Main_OnCommand(id, 0); };
    const ActionSetup setup = GetActionSetup(action.id);
    measurement.prepare = [prepare = setup.prepare] {
      FindTracks();
      if (prepare != nullptr) {
        prepare();
      }
    };
    measurement.reset = setup.reset;
    measurements->push_back(std::move(measurement));
  }

  // One ruler mode, as they are much alike.
  CostMeasurement ruler = OnProject("Main_OnCommand");
  ruler.condition =
      absl::StrCat("View: Time unit for ruler, ", ruler.condition);
  ruler.prepare = &FindRulerModes;
  ruler.call = [] { Main_OnCommand(g_other_ruler_mode, 0); };
  ruler.reset = [] { Main_OnCommand(g_ruler_mode, 0); };
  measurements->push_back(std::move(ruler));
}

constexpr UnmeasuredFunction kUnmeasured[] = {
    {"CreateMIDIInput", "The machine's ports, opened as the surface starts"},
    {"CreateMIDIOutput", "The machine's ports, opened as the surface starts"},
    {"GetMIDIInputName", "The machine's ports, read as the surface starts"},
    {"GetMIDIOutputName", "The machine's ports, read as the surface starts"},
    {"GetNumMIDIInputs", "The machine's ports, read as the surface starts"},
    {"GetNumMIDIOutputs", "The machine's ports, read as the surface starts"},
    {"ShowConsoleMsg", "Opens a window"},
};

}  // namespace

std::vector<CostMeasurement> GetCostMeasurements() {
  std::vector<CostMeasurement> measurements;
  AddReads(&measurements);
  AddChanges(&measurements);
  AddActions(&measurements);
  return measurements;
}

absl::Span<const UnmeasuredFunction> GetUnmeasuredFunctions() {
  return kUnmeasured;
}

}  // namespace jpr
