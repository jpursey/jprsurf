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
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"

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
// A fake REAPER, just big enough for the calls these tests make
//------------------------------------------------------------------------------

// What a MediaTrack* points to in these tests.
struct FakeTrack {
  int number;  // As REAPER's IP_TRACKNUMBER: -1 for the master track.
  std::string name;
};

MediaTrack* ToMediaTrack(FakeTrack& track) {
  return reinterpret_cast<MediaTrack*>(&track);
}

FakeTrack& FromMediaTrack(MediaTrack* track) {
  return *reinterpret_cast<FakeTrack*>(track);
}

void CopyText(std::string_view text, char* out) {
  text.copy(out, text.size());
  out[text.size()] = '\0';
}

// The surface REAPER created, which SetTrackUIMute() notifies.
IReaperControlSurface* g_surface = nullptr;

// The last mute the surface was set to.
bool g_surface_mute = false;

// The master track, which GetParentTrack() returns.
FakeTrack g_master_track = {-1, "MASTER"};

double FakeGetMediaTrackInfo_Value(MediaTrack* track, const char* parmname) {
  return FromMediaTrack(track).number;
}

// How many times a track's name has been read.
int g_name_reads = 0;

bool FakeGetSetMediaTrackInfo_String(MediaTrack* track, const char* parmname,
                                     char* value, bool set_new_value) {
  if (set_new_value) {
    FromMediaTrack(track).name = value;
  } else {
    ++g_name_reads;
    CopyText(FromMediaTrack(track).name, value);
  }
  return true;
}

int FakeCountTracks(ReaProject* project) { return 12; }

MediaTrack* FakeGetParentTrack(MediaTrack* track) {
  return ToMediaTrack(g_master_track);
}

bool FakeGetTrackUIVolPan(MediaTrack* track, double* volume, double* pan) {
  *volume = 0.5;
  *pan = -0.25;
  return true;
}

void FakeMkvolstr(char* text, double volume) { CopyText("-6.02dB", text); }

// There are no MIDI inputs, so this always fails, leaving `name` unwritten.
bool FakeGetMIDIInputName(int device, char* name, int name_size) {
  return false;
}

int FakeSetTrackUIMute(MediaTrack* track, int mute, int igngroupflags) {
  g_surface->SetSurfaceMute(track, mute != 0);
  return mute;
}

// What the surface does when REAPER calls its Run() or SetTrackListChange():
// the calls a test makes, as JPRSurf only calls REAPER from inside a callback.
std::function<void()> g_surface_calls = [] {};

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

//------------------------------------------------------------------------------
// Tests
//------------------------------------------------------------------------------

// The first line of every trace, as each test starts by creating the surface.
constexpr char kCreateLine[] = R"(csurf/Create("TEST", "config", out) -> )";

class ReaperTraceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ::GetMediaTrackInfo_Value = &FakeGetMediaTrackInfo_Value;
    ::GetSetMediaTrackInfo_String = &FakeGetSetMediaTrackInfo_String;
    ::CountTracks = &FakeCountTracks;
    ::GetParentTrack = &FakeGetParentTrack;
    ::GetTrackUIVolPan = &FakeGetTrackUIVolPan;
    ::mkvolstr = &FakeMkvolstr;
    ::GetMIDIInputName = &FakeGetMIDIInputName;
    ::SetTrackUIMute = &FakeSetTrackUIMute;
    trace_ = std::make_unique<ReaperTrace>(path_);

    reaper_csurf_reg_t* reg =
        ReaperTrace::TraceSurfaceRegistration(&g_test_surface_reg);
    ASSERT_NE(reg, &g_test_surface_reg);
    g_surface = reg->create("TEST", "config", nullptr);
    ASSERT_NE(g_surface, nullptr);
  }

  void TearDown() override {
    trace_.reset();
    delete g_surface;
    g_surface = nullptr;
    g_surface_calls = [] {};
    g_surface_mute = false;
    g_name_reads = 0;
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
  FakeTrack drums_ = {3, "Drums"};
  std::unique_ptr<ReaperTrace> trace_;
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
    EXPECT_EQ(CountTracks(nullptr), 12);
    EXPECT_EQ(GetParentTrack(ToMediaTrack(drums_)),
              ToMediaTrack(g_master_track));
  };
  g_surface->SetTrackListChange();

  EXPECT_THAT(
      ReadTrace(),
      ElementsAre(
          StartsWith(kCreateLine), "csurf/SetTrackListChange()",
          R"(  GetTrackUIVolPan(track 3 "Drums", out, out) -> true; out: 0.5, -0.25)",
          R"(  mkvolstr(out, 0.5) -> out: "-6.02dB")",
          R"(  CountTracks(null) -> 12)",
          R"(  GetParentTrack(track 3 "Drums") -> master)"));
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
  g_surface->Run();

  // Once for the reads and the change (which is formatted before it is made),
  // and once for the read after it.
  EXPECT_EQ(g_name_reads, 2);
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
  g_surface->SetTrackListChange();

  EXPECT_THAT(ReadTrace(),
              ElementsAre(StartsWith(kCreateLine), "csurf/SetTrackListChange()",
                          R"(  GetMIDIInputName(5, out, 4) -> false)"));
}

TEST_F(ReaperTraceTest, TracesSurfaceCallsInsideCalls) {
  g_surface_calls = [this] {
    EXPECT_EQ(SetTrackUIMute(ToMediaTrack(drums_), 1, 0), 1);
  };
  g_surface->Run();
  EXPECT_TRUE(g_surface_mute);
  EXPECT_EQ(g_surface->Extended(CSURF_EXT_SETLASTTOUCHEDTRACK,
                                ToMediaTrack(drums_), nullptr, nullptr),
            1);
  delete g_surface;
  g_surface = nullptr;

  EXPECT_THAT(
      ReadTrace(),
      ElementsAre(
          AllOf(StartsWith(kCreateLine), EndsWith("; out: null")),
          "csurf/Run()", R"(  SetTrackUIMute(track 3 "Drums", 1, 0))",
          R"(    csurf/SetSurfaceMute(track 3 "Drums", true))",
          R"(  SetTrackUIMute -> 1)",
          R"(csurf/Extended(CSURF_EXT_SETLASTTOUCHEDTRACK, track 3 "Drums", null, null) -> 1)",
          "csurf/Destroy()"));
}

TEST_F(ReaperTraceTest, LeavesOutQuietCalls) {
  g_surface_calls = [] { CountTracks(nullptr); };
  g_surface->Run();
  g_surface->Run();
  g_surface->IsKeyDown(VK_SHIFT);
  g_surface_calls = [] {};
  g_surface->SetTrackListChange();
  g_surface->Run();
  g_surface->Run();

  EXPECT_THAT(
      ReadTrace(),
      ElementsAre(StartsWith(kCreateLine), "(3 quiet calls left out)",
                  "csurf/SetTrackListChange()", "(2 quiet calls left out)"));
}

TEST_F(ReaperTraceTest, StopsTracingWhenDestroyed) {
  trace_.reset();

  EXPECT_EQ(::CountTracks, &FakeCountTracks);
  EXPECT_EQ(ReaperTrace::TraceSurfaceRegistration(&g_test_surface_reg),
            &g_test_surface_reg);

  // A surface that outlives the trace still works.
  g_surface->SetSurfaceMute(ToMediaTrack(drums_), true);
  EXPECT_TRUE(g_surface_mute);
}

}  // namespace
}  // namespace jpr
