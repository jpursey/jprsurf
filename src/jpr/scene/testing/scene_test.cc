// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/testing/scene_test.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "jpr/common/control_surface.h"
#include "jpr/common/testing/reaper_actions.h"
#include "jpr/device/control.h"

namespace jpr {

namespace {

// The fixture that exists, if any, whose scene a new surface runs.
SceneTest* g_scene_test = nullptr;

}  // namespace

//------------------------------------------------------------------------------
// Listener
//------------------------------------------------------------------------------

// The surface's listener, which activates the fixture's scene while the
// surface exists. Each run, it runs the devices, delivers the input given
// since the last run, and runs the scene, as PluginSurface does.
class SceneTest::Listener final : public ControlSurfaceListener {
 public:
  explicit Listener(SceneTest* test) : test_(test) {
    test_->scene_.Activate(test_->scene_runner_);
  }
  ~Listener() override { test_->scene_.Deactivate(); }

  static std::unique_ptr<ControlSurfaceListener> Create(
      std::string_view config) {
    return std::make_unique<Listener>(g_scene_test);
  }

  void OnRun(const RunTime& time) override {
    test_->device_runner_.Run(time);
    test_->DeliverInput();
    test_->scene_runner_.Run(time);
  }

 private:
  SceneTest* const test_;
};

//------------------------------------------------------------------------------
// Setting up
//------------------------------------------------------------------------------

SceneTest::SceneTest() : device_(AddDevice()) {
  g_scene_test = this;
  AddReaperActions(&reaper_);
  EXPECT_TRUE(ControlSurface::Register(reaper_.GetPluginInfo(),
                                       {.type_string = "SCENETEST",
                                        .description = "Scene test",
                                        .create_listener = &Listener::Create}));
}

SceneTest::~SceneTest() { g_scene_test = nullptr; }

void SceneTest::TearDown() { RemoveSurface(); }

FakeDevice* SceneTest::AddDevice() {
  auto device = std::make_unique<FakeDevice>(device_runner_);
  FakeDevice* added = device.get();
  scene_.AddDevice(kDeviceName, std::move(device));
  return added;
}

std::string SceneTest::GetControlName(std::string_view name) {
  return absl::StrCat(kDeviceName, "/", name);
}

void SceneTest::AddSurface() {
  surface_ = reaper_.AddSurface();
  ASSERT_NE(surface_, nullptr);
  surface_->SetTrackListChange();
  RunUntilShown();
}

void SceneTest::RemoveSurface() { surface_.reset(); }

//------------------------------------------------------------------------------
// Running
//------------------------------------------------------------------------------

void SceneTest::RunUntilShown() {
  surface_->Run();
  surface_->Run();
}

//------------------------------------------------------------------------------
// Input
//------------------------------------------------------------------------------

void SceneTest::Press(const FakeDevice::FakeControl& control) {
  input_.push_back([input = control.press_input] { input->Press(); });
}

void SceneTest::Release(const FakeDevice::FakeControl& control) {
  input_.push_back([input = control.press_input] { input->Release(); });
}

void SceneTest::DeliverInput() {
  for (absl::AnyInvocable<void()>& input : input_) {
    input();
  }
  input_.clear();
}

//------------------------------------------------------------------------------
// Presses
//------------------------------------------------------------------------------

void SceneTest::Tap(const FakeDevice::FakeControl& control) {
  Press(control);
  surface_->Run();
  Release(control);
  Settle();
}

void SceneTest::DoublePress(const FakeDevice::FakeControl& control) {
  Press(control);
  surface_->Run();
  Release(control);
  surface_->Run();
  Tap(control);
}

void SceneTest::LongPress(const FakeDevice::FakeControl& control) {
  Hold(control);
  Release(control);
  Settle();
}

void SceneTest::Hold(const FakeDevice::FakeControl& control) {
  // A run past a long press.
  Press(control);
  surface_->RunFor(absl::Seconds(Control::kLongPressDurationSecs) +
                   FakeReaper::GetRunTime());
}

void SceneTest::Settle() {
  // A press held back in case it is a double press is acted on in the last run
  // of the double press window.
  surface_->RunFor(absl::Seconds(Control::kDoublePressWindowSecs));
  RunUntilShown();
}

}  // namespace jpr
