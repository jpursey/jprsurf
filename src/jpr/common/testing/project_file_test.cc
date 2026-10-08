// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/project_file.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

// Returns each line of `text` that starts with `key` and a space, without its
// indent.
std::vector<std::string> Lines(std::string_view text, std::string_view key) {
  std::vector<std::string> lines;
  for (std::string_view line : absl::StrSplit(text, '\n')) {
    line = absl::StripLeadingAsciiWhitespace(line);
    if (absl::StartsWith(line, key) && line.size() > key.size() &&
        line[key.size()] == ' ') {
      lines.emplace_back(line);
    }
  }
  return lines;
}

TEST(ProjectFileTest, AnEmptyProjectIsTheMaster) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  EXPECT_EQ(WriteProjectFile(*project),
            "<REAPER_PROJECT 0.1 \"7.0/x64\" 0\n"
            "  CURSOR 0\n"
            "  GLOBAL_AUTO -1\n"
            "  TEMPO 120 4 4\n"
            "  MASTERAUTOMODE 0\n"
            "  MASTERPEAKCOL 16576\n"
            "  MASTERMUTESOLO 0\n"
            "  MASTER_VOLUME 1 0 -1 -1 1\n"
            "  MASTER_SEL 0\n"
            ">\n");
}

TEST(ProjectFileTest, WritesTheProjectsState) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  project->SetCursorPosition(2.5);
  project->SetAutomationOverride(3);
  FakeTrack* master = project->GetMasterTrack();
  master->auto_mode = 1;
  master->color = 0x010000FF;
  master->mute = true;
  master->volume = 0.5;
  master->pan = -0.25;
  master->selected = true;
  EXPECT_EQ(WriteProjectFile(*project),
            "<REAPER_PROJECT 0.1 \"7.0/x64\" 0\n"
            "  CURSOR 2.5\n"
            "  GLOBAL_AUTO 3\n"
            "  TEMPO 120 4 4\n"
            "  MASTERAUTOMODE 1\n"
            "  MASTERPEAKCOL 16777471\n"
            "  MASTERMUTESOLO 1\n"
            "  MASTER_VOLUME 0.5 -0.25 -1 -1 1\n"
            "  MASTER_SEL 1\n"
            ">\n");
}

TEST(ProjectFileTest, WritesEachTracksState) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  project->AddTrack("A");
  FakeTrack* b = project->AddTrack("B");
  b->color = 0x01FF0000;
  b->volume = 0.1;
  b->pan = 0.75;
  b->mute = true;
  b->solo = true;
  b->rec_arm = true;
  b->selected = true;
  b->auto_mode = 4;
  b->show_in_mixer = false;
  b->show_in_tcp = false;

  const std::string text = WriteProjectFile(*project);
  EXPECT_THAT(text,
              HasSubstr("  <TRACK {00000002-0001-0000-0000-000000000000}\n"
                        "    NAME \"A\"\n"
                        "    PEAKCOL 16576\n"
                        "    AUTOMODE 0\n"
                        "    VOLPAN 1 0 -1 -1 1\n"
                        "    MUTESOLO 0 0 0\n"
                        "    ISBUS 0 0\n"
                        "    SHOWINMIX 1 0.6667 0.5 1 0.5 0 0 0\n"
                        "    SEL 0\n"
                        "    REC 0 0 1 0 0 0 0\n"
                        "    TRACKID {00000002-0001-0000-0000-000000000000}\n"
                        "  >\n"));
  EXPECT_THAT(text,
              HasSubstr("  <TRACK {00000003-0001-0000-0000-000000000000}\n"
                        "    NAME \"B\"\n"
                        "    PEAKCOL 33488896\n"
                        "    AUTOMODE 4\n"
                        "    VOLPAN 0.1 0.75 -1 -1 1\n"
                        "    MUTESOLO 1 1 0\n"
                        "    ISBUS 0 0\n"
                        "    SHOWINMIX 0 0.6667 0.5 0 0.5 0 0 0\n"
                        "    SEL 1\n"
                        "    REC 1 0 1 0 0 0 0\n"
                        "    TRACKID {00000003-0001-0000-0000-000000000000}\n"
                        "  >\n"));
  EXPECT_LT(text.find("NAME \"A\""), text.find("NAME \"B\""));
}

TEST(ProjectFileTest, NamesAreQuotedWithAQuoteTheyDontContain) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  project->AddTrack("");
  project->AddTrack("Lead vocal");
  project->AddTrack("The \"best\" take");
  project->AddTrack("Take \"2\" isn't it");
  EXPECT_THAT(
      Lines(WriteProjectFile(*project), "NAME"),
      ElementsAre("NAME \"\"", "NAME \"Lead vocal\"",
                  "NAME 'The \"best\" take'", "NAME `Take \"2\" isn't it`"));

  FakeTrack* last = project->AddTrack("`All` \"three\" aren't");
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "every kind of quote");
  last->name = "Two\nlines";
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "has a line break");
}

TEST(ProjectFileTest, AColorWithoutItsFlagFailsTheTest) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  project->AddTrack("A")->color = 0xFF0000;
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "A's color");
}

TEST(ProjectFileTest, FoldersOpenBeforeTheirTracksAndCloseAfterTheLast) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  FakeTrack* t1 = project->AddTrack("T1");
  FakeTrack* t11 = project->AddTrack("T1.1", t1);
  project->AddTrack("T1.1.1", t11);
  project->AddTrack("T1.2", t1);
  FakeTrack* t2 = project->AddTrack("T2");
  FakeTrack* t21 = project->AddTrack("T2.1", t2);
  project->AddTrack("T2.1.1", t21);
  EXPECT_THAT(Lines(WriteProjectFile(*project), "ISBUS"),
              ElementsAre("ISBUS 1 1", "ISBUS 1 1", "ISBUS 2 -1", "ISBUS 2 -1",
                          "ISBUS 1 1", "ISBUS 1 1", "ISBUS 2 -2"));
}

TEST(ProjectFileTest, ReceivesNameTheTrackTheyComeFrom) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  FakeTrack* a = project->AddTrack("A");
  FakeTrack* b = project->AddTrack("B");
  FakeTrack* c = project->AddTrack("C");
  FakeRoute* b_to_c = project->AddSend(b, c);
  b_to_c->volume = 0.5;
  b_to_c->pan = -1.0;
  b_to_c->mute = true;
  project->AddSend(a, c);
  project->AddSend(c, a);

  const std::string text = WriteProjectFile(*project);
  EXPECT_THAT(Lines(text, "AUXRECV"),
              ElementsAre("AUXRECV 2 0 1 0 0 0 0 0 0 -1:U 31 -1 ''",
                          "AUXRECV 1 0 0.5 -1 1 0 0 0 0 -1:U 31 -1 ''",
                          "AUXRECV 0 0 1 0 0 0 0 0 0 -1:U 31 -1 ''"));
  EXPECT_LT(text.find("AUXRECV 2"), text.find("NAME \"B\""));
}

TEST(ProjectFileTest, WritesHardwareOutputs) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  FakeTrack* a = project->AddTrack("A");
  FakeRoute* output = project->AddHardwareOutput(a);
  output->volume = 0.25;
  output->pan = 0.5;
  output->mute = true;
  project->AddHardwareOutput(project->GetMasterTrack());

  const std::string text = WriteProjectFile(*project);
  EXPECT_THAT(Lines(text, "HWOUT"), ElementsAre("HWOUT 0 0 0.25 0.5 1 0 0 -1"));
  EXPECT_THAT(Lines(text, "MASTERHWOUT"),
              ElementsAre("MASTERHWOUT 0 0 1 0 0 0 0 -1"));
}

TEST(ProjectFileTest, GroupsLeadAndFollowEachGroupedProperty) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  project->AddTrack("A")->group = 1;
  project->AddTrack("B")->group = 32;
  project->AddTrack("C");
  EXPECT_THAT(Lines(WriteProjectFile(*project), "GROUP_FLAGS"),
              ElementsAre("GROUP_FLAGS 1 1 1 1 1 1 1 1 1 1",
                          "GROUP_FLAGS 2147483648 2147483648 2147483648 "
                          "2147483648 2147483648 2147483648 2147483648 "
                          "2147483648 2147483648 2147483648"));

  FakeTrack* d = project->AddTrack("D");
  d->group = 33;
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "isn't from 1 to 32");
  d->group = -1;
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "isn't from 1 to 32");
}

TEST(ProjectFileTest, SelectedItemsAreOnTheFirstTrack) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  project->SetSelectedItemCount(2);
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "without a track");

  project->AddTrack("A");
  project->AddTrack("B");
  const std::string text = WriteProjectFile(*project);
  EXPECT_THAT(text, HasSubstr("    <ITEM\n"
                              "      POSITION 0\n"
                              "      LENGTH 1\n"
                              "      SEL 1\n"
                              "    >\n"
                              "    <ITEM\n"
                              "      POSITION 1\n"
                              "      LENGTH 1\n"
                              "      SEL 1\n"
                              "    >\n"
                              "  >\n"
                              "  <TRACK"));
}

TEST(ProjectFileTest, DeletedTracksAreLeftOut) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  FakeTrack* a = project->AddTrack("A");
  FakeTrack* b = project->AddTrack("B");
  project->AddSend(a, b);
  project->DeleteTrack(a);
  const std::string text = WriteProjectFile(*project);
  EXPECT_THAT(Lines(text, "NAME"), ElementsAre("NAME \"B\""));
  EXPECT_THAT(Lines(text, "AUXRECV"), IsEmpty());
}

TEST(ProjectFileTest, StateAProjectDoesntOpenWithFailsTheTest) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  project->SetPlayState(1);
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "a play state");
  project->SetPlayState(0);
  project->SetPlayPosition(1.0);
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "a play position");
  project->SetPlayPosition(0.0);
  project->SetRedo("Change");
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "a redo");
  project->SetRedo("");
  project->SetDirty(true);
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "changes to save");
  project->SetDirty(false);
  project->AddTrack("A")->peak = {0.5, 0.5};
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "A's peak");
}

TEST(ProjectFileTest, UndoPointsFailTheTest) {
  // Only the API adds undo points, so this needs the fake.
  FakeReaper reaper;
  Undo_OnStateChangeEx("Change", UNDO_STATE_TRACKCFG, -1);
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(reaper.GetProject()), "undo points");
}

TEST(ProjectFileTest, MasterStateAProjectCantHoldFailsTheTest) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  FakeTrack* master = project->GetMasterTrack();
  master->name = "Main";
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "the master's name");
  master->name = "MASTER";
  master->solo = true;
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "the master's solo");
  master->solo = false;
  master->rec_arm = true;
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "the master's rec arm");
  master->rec_arm = false;
  master->group = 1;
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "the master's group");
  master->group = 0;
  master->show_in_tcp = false;
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "the master hidden");
  master->show_in_tcp = true;
  master->peak = {1.0, 0.0};
  EXPECT_NONFATAL_FAILURE(WriteProjectFile(*project), "the master's peak");
}

}  // namespace
}  // namespace jpr
