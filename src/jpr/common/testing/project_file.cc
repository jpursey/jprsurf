// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/project_file.h"

#include <array>
#include <charconv>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gtest/gtest.h"
#include "jpr/common/guid.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"

namespace jpr {
namespace {

// The PEAKCOL REAPER writes for a track with no color of its own.
constexpr int kNoColor = 16576;

// GROUP_FLAGS has a mask of groups for each property a track leads or follows
// in, in this order: volume, pan, mute, solo, and rec arm, each leading then
// following. The rest are left out, as no group has them. Each mask holds
// groups 1 to 32.
constexpr int kGroupFlagCount = 10;
constexpr int kMaxGroup = 32;

// Returns `value` as the shortest text that reads back as the same value.
std::string Number(double value) {
  std::array<char, 32> text;
  const auto result =
      std::to_chars(text.data(), text.data() + text.size(), value);
  return std::string(text.data(), result.ptr);
}

std::string Flag(bool value) { return value ? "1" : "0"; }

// A color of the track's own, as GetTrackColor() returns it, has this flag.
constexpr int kColorFlag = 0x01000000;

// Returns a track's color as PEAKCOL and MASTERPEAKCOL hold it.
int PeakColor(const FakeTrack* track) {
  return track->color != 0 ? track->color : kNoColor;
}

// Returns a track's volume and pan as VOLPAN and MASTER_VOLUME hold them.
std::string VolumePan(const FakeTrack* track) {
  return absl::StrCat(Number(track->volume), " ", Number(track->pan),
                      " -1 -1 1");
}

// Writes `project` as RPP text.
class ProjectFileWriter final {
 public:
  explicit ProjectFileWriter(const FakeProject& project) : project_(project) {}

  std::string Write();

 private:
  // Fails the test for `what`, which can't be written.
  void Fail(std::string_view what);

  // Fails the test if `track`, called `name`, has a peak, or a color without
  // kColorFlag, which REAPER would read as no color.
  void CheckTrack(const FakeTrack* track, std::string_view name);

  // Returns `text` in quotes, as REAPER writes strings: the first of ", ', and
  // ` that it doesn't contain.
  std::string Quote(std::string_view text);

  // Returns the number of folders `track` is in.
  int GetDepth(const FakeTrack* track) const;

  // Each appends a part of the project.
  void AppendProject();
  void AppendMaster();
  void AppendTrack(int index);
  void AppendHardwareOutputs(std::string_view key, const FakeTrack* track,
                             std::string_view indent);
  void AppendSelectedItems();

  const FakeProject& project_;
  std::string text_;
};

std::string ProjectFileWriter::Write() {
  text_ = "<REAPER_PROJECT 0.1 \"7.0/x64\" 0\n";
  AppendProject();
  AppendMaster();
  for (int i = 0; i < project_.GetTrackCount(); ++i) {
    AppendTrack(i);
  }
  absl::StrAppend(&text_, ">\n");
  return std::move(text_);
}

void ProjectFileWriter::Fail(std::string_view what) {
  ADD_FAILURE() << "WriteProjectFile() can't write " << what;
}

void ProjectFileWriter::CheckTrack(const FakeTrack* track,
                                   std::string_view name) {
  if (track->peak != decltype(track->peak){}) {
    Fail(absl::StrCat(name, "'s peak, as a project opens stopped"));
  }
  if (track->color != 0 && (track->color & kColorFlag) == 0) {
    Fail(absl::StrCat(name, "'s color, which doesn't have 0x01000000 set"));
  }
}

std::string ProjectFileWriter::Quote(std::string_view text) {
  if (text.find_first_of("\r\n") != std::string_view::npos) {
    Fail(absl::StrCat(text, ", which has a line break"));
    return "\"\"";
  }
  for (char quote : {'"', '\'', '`'}) {
    if (text.find(quote) == std::string_view::npos) {
      return absl::StrCat(std::string_view(&quote, 1), text,
                          std::string_view(&quote, 1));
    }
  }
  Fail(absl::StrCat(text, ", which contains every kind of quote"));
  return "\"\"";
}

int ProjectFileWriter::GetDepth(const FakeTrack* track) const {
  int depth = 0;
  for (const FakeTrack* parent = project_.GetParentTrack(track);
       parent != nullptr; parent = project_.GetParentTrack(parent)) {
    ++depth;
  }
  return depth;
}

void ProjectFileWriter::AppendProject() {
  if (project_.GetPlayState() != 0) {
    Fail("a play state, as a project opens stopped");
  }
  if (project_.GetPlayPosition() != 0.0) {
    Fail("a play position, as a project opens stopped");
  }
  if (project_.GetUndoCount() > 0) {
    Fail("undo points, as a project opens with none");
  }
  if (!project_.GetRedo().empty()) {
    Fail("a redo, as a project opens with none");
  }
  if (project_.IsDirty()) {
    Fail("changes to save, as a project opens with none");
  }
  if (project_.GetSelectedItemCount() > 0 && project_.GetTrackCount() == 0) {
    Fail("selected media items without a track to put them on");
  }
  absl::StrAppend(&text_, "  CURSOR ", Number(project_.GetCursorPosition()),
                  "\n");
  absl::StrAppend(&text_, "  GLOBAL_AUTO ", project_.GetAutomationOverride(),
                  "\n");
  absl::StrAppend(&text_, "  TEMPO ", Number(FakeProject::kBeatsPerMinute),
                  " 4 4\n");

  // The pan mode every fake track has, for the tracks, and in AppendMaster(),
  // the master. Without them, a project has REAPER 3's.
  absl::StrAppend(&text_, "  PANMODE ", kBalancePanMode, "\n");
}

void ProjectFileWriter::AppendMaster() {
  const FakeTrack* master = project_.GetMasterTrack();
  if (master->name != "MASTER") {
    Fail("the master's name, which REAPER's master can't change");
  }
  if (master->solo) {
    Fail("the master's solo, which REAPER's master doesn't have");
  }
  if (master->rec_arm) {
    Fail("the master's rec arm, which REAPER's master doesn't have");
  }
  if (master->group != 0) {
    Fail("the master's group");
  }
  if (!master->show_in_mixer) {
    Fail("the master hidden in the mixer, which shows the master its own way");
  }
  CheckTrack(master, "the master");
  absl::StrAppend(&text_, "  MASTERAUTOMODE ", master->auto_mode, "\n");
  absl::StrAppend(&text_, "  MASTERPEAKCOL ", PeakColor(master), "\n");
  absl::StrAppend(&text_, "  MASTERMUTESOLO ", Flag(master->mute), "\n");

  // Its first field shows the master in the track panel, which View: Toggle
  // master track visible toggles. Without it, the master is hidden.
  absl::StrAppend(&text_, "  MASTERTRACKVIEW ", Flag(master->show_in_tcp),
                  " 0.6667 0.5 0.5 0 0 0 0 0 0 0 0 0 0 0\n");
  absl::StrAppend(&text_, "  MASTER_VOLUME ", VolumePan(master), "\n");
  absl::StrAppend(&text_, "  MASTER_PANMODE ", kBalancePanMode, "\n");
  absl::StrAppend(&text_, "  MASTER_SEL ", Flag(master->selected), "\n");
  AppendHardwareOutputs("MASTERHWOUT", master, "  ");
}

void ProjectFileWriter::AppendTrack(int index) {
  const FakeTrack* track = project_.GetTrack(index);
  CheckTrack(track, track->name);
  const std::string guid = FormatGuid(project_.GetGuid(track));
  absl::StrAppend(&text_, "  <TRACK ", guid, "\n");
  absl::StrAppend(&text_, "    NAME ", Quote(track->name), "\n");
  absl::StrAppend(&text_, "    PEAKCOL ", PeakColor(track), "\n");
  absl::StrAppend(&text_, "    AUTOMODE ", track->auto_mode, "\n");
  absl::StrAppend(&text_, "    VOLPAN ", VolumePan(track), "\n");

  // Solo is 1, or 2 in place.
  const int solo = track->solo ? (track->solo_in_place ? 2 : 1) : 0;
  absl::StrAppend(&text_, "    MUTESOLO ", Flag(track->mute), " ", solo,
                  " 0\n");

  // A folder opens before its first track, and each folder its last track
  // ends is closed after it.
  const int depth = GetDepth(track);
  const int next_depth = index + 1 < project_.GetTrackCount()
                             ? GetDepth(project_.GetTrack(index + 1))
                             : 0;
  if (project_.IsFolder(track)) {
    absl::StrAppend(&text_, "    ISBUS 1 1\n");
  } else if (next_depth < depth) {
    absl::StrAppend(&text_, "    ISBUS 2 ", next_depth - depth, "\n");
  } else {
    absl::StrAppend(&text_, "    ISBUS 0 0\n");
  }

  absl::StrAppend(&text_, "    SHOWINMIX ", Flag(track->show_in_mixer),
                  " 0.6667 0.5 ", Flag(track->show_in_tcp), " 0.5 0 0 0\n");
  absl::StrAppend(&text_, "    SEL ", Flag(track->selected), "\n");

  // Rec arm, then the input, and input monitoring off, as the fake has none,
  // where REAPER's new tracks have it on.
  absl::StrAppend(&text_, "    REC ", Flag(track->rec_arm), " 0 0 0 0 0 0\n");
  absl::StrAppend(&text_, "    TRACKID ", guid, "\n");
  if (track->group < 0 || track->group > kMaxGroup) {
    Fail(absl::StrCat(track->name, "'s group, ", track->group,
                      ", which isn't from 1 to ", kMaxGroup));
  } else if (track->group > 0) {
    std::vector<unsigned> masks(kGroupFlagCount, 1u << (track->group - 1));
    absl::StrAppend(&text_, "    GROUP_FLAGS ", absl::StrJoin(masks, " "),
                    "\n");
  }

  // Receives, in order, each naming the track it comes from.
  for (const FakeRoute* receive : project_.GetReceives(track)) {
    absl::StrAppend(&text_, "    AUXRECV ", project_.FindTrack(receive->source),
                    " 0 ", Number(receive->volume), " ", Number(receive->pan),
                    " ", Flag(receive->mute), " 0 0 0 0 -1:U 31 -1 ''\n");
  }
  AppendHardwareOutputs("HWOUT", track, "    ");
  if (index == 0) {
    AppendSelectedItems();
  }
  absl::StrAppend(&text_, "  >\n");
}

void ProjectFileWriter::AppendHardwareOutputs(std::string_view key,
                                              const FakeTrack* track,
                                              std::string_view indent) {
  for (const FakeRoute* output : project_.GetHardwareOutputs(track)) {
    absl::StrAppend(&text_, indent, key, " 0 0 ", Number(output->volume), " ",
                    Number(output->pan), " ", Flag(output->mute), " 0 0 -1\n");
  }
}

void ProjectFileWriter::AppendSelectedItems() {
  for (int i = 0; i < project_.GetSelectedItemCount(); ++i) {
    absl::StrAppend(&text_, "    <ITEM\n");
    absl::StrAppend(&text_, "      POSITION ", i, "\n");
    absl::StrAppend(&text_, "      LENGTH 1\n");
    absl::StrAppend(&text_, "      SEL 1\n");
    absl::StrAppend(&text_, "    >\n");
  }
}

}  // namespace

std::string WriteProjectFile(const FakeProject& project) {
  return ProjectFileWriter(project).Write();
}

}  // namespace jpr
