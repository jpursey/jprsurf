// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/reaper_trace.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gb/base/function_hook.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/surface_notifier.h"
#include "jpr/common/testing/test_control_surface.h"

namespace jpr {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::StartsWith;

// The width of the time column at the start of each line of a trace, as in
// reaper_trace.cc.
constexpr int kTimeWidth = 11;

//------------------------------------------------------------------------------
// A control surface, as the plugin registers
//------------------------------------------------------------------------------

// What the surface does when REAPER calls its Run() or SetTrackListChange():
// the calls a test makes, as JPRSurf only calls REAPER from inside a callback.
std::function<void()> g_surface_calls = [] {};

// The last mute the surface was set to.
bool g_surface_mute = false;

class TestSurface final : public IReaperControlSurface {
 public:
  const char* GetTypeString() override { return "TEST"; }
  const char* GetDescString() override { return "Test surface"; }
  const char* GetConfigString() override { return ""; }
  void Run() override { g_surface_calls(); }
  void SetTrackListChange() override { g_surface_calls(); }
  void SetSurfaceMute(MediaTrack* track, bool mute) override {
    g_surface_mute = mute;
  }
  int Extended(int call, void* param1, void* param2, void* param3) override {
    return call == CSURF_EXT_SETLASTTOUCHEDTRACK ? 1 : 0;
  }
};

IReaperControlSurface* CreateTestSurface(const char* type_string,
                                         const char* config_string,
                                         int* err_stats) {
  return new TestSurface;
}

reaper_csurf_reg_t g_test_surface_reg = {"TEST", "Test surface",
                                         &CreateTestSurface, nullptr};

// Counts the times a track's name is read, under the trace.
struct NameReadCounter {
  bool Call(decltype(GetSetMediaTrackInfo_String) original, MediaTrack* track,
            const char* name, char* value, bool set) {
    if (!set) {
      ++count;
    }
    return original(track, name, value, set);
  }

  int count = 0;
};

//------------------------------------------------------------------------------
// Tests
//------------------------------------------------------------------------------

// The first line of every trace, as each test starts by creating the surface.
constexpr char kCreateLine[] = R"(csurf/Create("TEST", "config", out) -> )";

// The fake REAPER, with Drums as the third track, and a SurfaceNotifier for
// the calls REAPER makes back. Each test traces from creating the surface.
class ReaperTraceTest : public ::testing::Test {
 protected:
  ReaperTraceTest() {
    project_.AddTracks(2);
    drums_ = project_.AddTrack("Drums");
    drums_->volume = 0.5;
    drums_->pan = -0.25;
  }

  void SetUp() override {
    trace_ = std::make_unique<ReaperTrace>(path_);
    reaper_.GetPluginInfo().Register(
        "csurf", ReaperTrace::TraceSurfaceRegistration(&g_test_surface_reg));
    surface_ = reaper_.AddSurface("config");
    ASSERT_NE(surface_, nullptr);
  }

  void TearDown() override {
    g_surface_calls = [] {};
    g_surface_mute = false;
  }

  // Ends the trace, and returns its lines after the header, without the time
  // column (so nested calls keep their indent).
  std::vector<std::string> ReadTrace() {
    trace_.reset();
    std::ifstream file(path_);
    std::string line;
    std::getline(file, line);
    EXPECT_THAT(line, StartsWith("JPRSurf REAPER trace, started "));
    std::vector<std::string> lines;
    while (std::getline(file, line) && line != "Trace ended") {
      lines.push_back(line.substr(kTimeWidth));
    }
    return lines;
  }

  const std::filesystem::path path_ =
      std::filesystem::path(::testing::TempDir()) / "reaper_trace_test.txt";
  FakeReaper reaper_;
  FakeProject& project_ = reaper_.GetProject();
  FakeTrack* drums_ = nullptr;
  SurfaceNotifier notifier_{&reaper_};
  decltype(CountTracks) const untraced_count_tracks_ = CountTracks;
  gb::FunctionHook<&GetSetMediaTrackInfo_String, NameReadCounter>
      name_read_counter_;
  std::unique_ptr<ReaperTrace> trace_;
  std::unique_ptr<TestControlSurface> surface_;
};

TEST_F(ReaperTraceTest, TracesResultsAndOutputs) {
  g_surface_calls = [this] {
    double volume = 0;
    double pan = 0;
    EXPECT_TRUE(GetTrackUIVolPan(ToMediaTrack(drums_), &volume, &pan));
    EXPECT_EQ(volume, 0.5);
    EXPECT_EQ(pan, -0.25);
    char text[64];
    mkvolstr(text, 0.5);
    EXPECT_STREQ(text, "-6.02dB");
    EXPECT_EQ(CountTracks(nullptr), 3);
    EXPECT_EQ(GetMasterTrack(nullptr), ToMediaTrack(project_.GetMasterTrack()));
  };
  surface_->SetTrackListChange();

  EXPECT_THAT(
      ReadTrace(),
      ElementsAre(
          StartsWith(kCreateLine), "csurf/SetTrackListChange()",
          R"(  GetTrackUIVolPan(track 3 "Drums", out, out) -> true; out: 0.5, -0.25)",
          R"(  mkvolstr(out, 0.5) -> out: "-6.02dB")",
          R"(  CountTracks(null) -> 3)",
          R"(  GetMasterTrack(null) -> master)"));
}

TEST_F(ReaperTraceTest, NamesTracksAgainOnlyAfterChanges) {
  g_surface_calls = [this] {
    double volume = 0;
    double pan = 0;
    GetTrackUIVolPan(ToMediaTrack(drums_), &volume, &pan);
    GetTrackUIVolPan(ToMediaTrack(drums_), &volume, &pan);
    char name[] = "Kick";
    GetSetMediaTrackInfo_String(ToMediaTrack(drums_), "P_NAME", name, true);
    GetTrackUIVolPan(ToMediaTrack(drums_), &volume, &pan);
  };
  surface_->Run();

  // Once for the reads and the change (which is formatted before it is made),
  // and once for the read after it.
  EXPECT_EQ(name_read_counter_.hook().count, 2);
  EXPECT_THAT(
      ReadTrace(),
      ElementsAre(
          StartsWith(kCreateLine), "csurf/Run()",
          R"(  GetTrackUIVolPan(track 3 "Drums", out, out) -> true; out: 0.5, -0.25)",
          R"(  GetTrackUIVolPan(track 3 "Drums", out, out) -> true; out: 0.5, -0.25)",
          R"(  GetSetMediaTrackInfo_String(track 3 "Drums", "P_NAME", out, true) -> true; out: "Kick")",
          R"(  GetTrackUIVolPan(track 3 "Kick", out, out) -> true; out: 0.5, -0.25)"));
}

TEST_F(ReaperTraceTest, SkipsOutputsWhenCallFails) {
  g_surface_calls = [] {
    // Not terminated, so reading it as text would run off the end.
    char name[4] = {'x', 'x', 'x', 'x'};
    EXPECT_FALSE(GetMIDIInputName(5, name, 4));
  };
  surface_->SetTrackListChange();

  EXPECT_THAT(ReadTrace(),
              ElementsAre(StartsWith(kCreateLine), "csurf/SetTrackListChange()",
                          R"(  GetMIDIInputName(5, out, 4) -> false)"));
}

TEST_F(ReaperTraceTest, TracesSurfaceCallsInsideCalls) {
  g_surface_calls = [this] {
    EXPECT_EQ(SetTrackUIMute(ToMediaTrack(drums_), 1, 0), 1);
  };
  surface_->Run();
  EXPECT_EQ(surface_->Extended(CSURF_EXT_SETLASTTOUCHEDTRACK,
                               ToMediaTrack(drums_), nullptr, nullptr),
            1);
  surface_.reset();

  EXPECT_THAT(
      ReadTrace(),
      ElementsAre(
          AllOf(StartsWith(kCreateLine), EndsWith("; out: 0")), "csurf/Run()",
          R"(  SetTrackUIMute(track 3 "Drums", 1, 0))",
          R"(    csurf/SetSurfaceSolo(master, false))",
          R"(  SetTrackUIMute -> 1)",
          R"(csurf/Extended(CSURF_EXT_SETLASTTOUCHEDTRACK, track 3 "Drums", null, null) -> 1)",
          "csurf/Destroy()"));
}

TEST_F(ReaperTraceTest, LeavesOutQuietCalls) {
  g_surface_calls = [] { CountTracks(nullptr); };
  surface_->Run();
  surface_->Run();
  surface_->IsKeyDown(VK_SHIFT);
  g_surface_calls = [] {};
  surface_->SetTrackListChange();
  surface_->Run();
  surface_->Run();

  EXPECT_THAT(
      ReadTrace(),
      ElementsAre(StartsWith(kCreateLine), "(3 quiet calls left out)",
                  "csurf/SetTrackListChange()", "(2 quiet calls left out)"));
}

TEST_F(ReaperTraceTest, StopsTracingWhenDestroyed) {
  trace_.reset();

  EXPECT_EQ(CountTracks, untraced_count_tracks_);
  EXPECT_EQ(ReaperTrace::TraceSurfaceRegistration(&g_test_surface_reg),
            &g_test_surface_reg);

  // A surface that outlives the trace still works.
  surface_->SetSurfaceMute(ToMediaTrack(drums_), true);
  EXPECT_TRUE(g_surface_mute);
}

}  // namespace
}  // namespace jpr
