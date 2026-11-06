// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/create_scene.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gb/test/log_error_guard.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/control_surface.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/reaper_actions.h"
#include "jpr/common/testing/surface_notifier.h"
#include "jpr/common/testing/test_control_surface.h"
#include "jpr/common/track.h"
#include "jpr/device/control.h"
#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/device/widget_definition.h"
#include "jpr/scene/command_properties.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/surface_spec.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_mapping.h"
#include "jpr/scene/view_property.h"

namespace jpr {
namespace {

using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::StartsWith;

using Button = FakeXTouch::Button;
using Light = FakeXTouch::Light;
using StripButton = FakeXTouch::StripButton;

class CreateSceneTest;
CreateSceneTest* g_create_scene_test = nullptr;

// Returns a spec with an X-Touch, whose widgets are its catalog's, and nothing
// else.
SurfaceSpec GetXTouchSpec() {
  return {.devices = {{.name = "xtouch",
                       .type = "XTouch",
                       .input_port = "X-Touch",
                       .output_port = "X-Touch"}},
          .widgets = {.includes = {{.path = "xtouch"}}}};
}

// Returns a view bound to the reference, with a list of its track's child
// tracks, and a child repeated over the strips, which `item` is.
ViewSpec ListView(std::string name, std::string reference, ViewSpec item = {}) {
  item.config.subject = View::ListItemSubject{};
  item.repeat = RepeatSpec{.array = "strip", .element = "strip"};
  return {.name = std::move(name),
          .config = {.subject = View::ReferenceSubject{std::move(reference)},
                     .list = View::ListConfig{.items = View::ChildTracks{}}},
          .children = {std::move(item)}};
}

// The fake REAPER, with an X-Touch on "X-Touch", and an extender on
// "X-Touch-Ext" once it is added. The surface runs the scene CreateScene()
// returns as PluginSurface runs its own.
class CreateSceneTest : public ::testing::Test {
 protected:
  CreateSceneTest() {
    g_create_scene_test = this;
    AddReaperActions(&reaper_);
    EXPECT_TRUE(ControlSurface::Register(
        reaper_.GetPluginInfo(), {.type_string = "CREATESCENETEST",
                                  .description = "CreateScene test",
                                  .create_listener = &Listener::Create}));
  }
  ~CreateSceneTest() override { g_create_scene_test = nullptr; }

  void TearDown() override { surface_.reset(); }

  // Lists the extender's ports, which MidiPorts lists as it is created.
  void AddExtender() {
    xtouch_ext_.emplace(reaper_, FakeXTouch::Type::kExtender, "X-Touch-Ext");
  }

  // Returns the scene created from the spec, on new ports, and sets `errors_`
  // to the errors.
  Scene* Create(const SurfaceSpec& spec) {
    scene_.reset();
    ports_.emplace();
    errors_.clear();
    scene_ = CreateScene(spec, *ports_, device_runner_, &errors_);
    return scene_.get();
  }

  // Adds the surface, which activates the scene, then runs until the X-Touch
  // shows the project.
  void AddSurface() {
    surface_ = reaper_.AddSurface();
    ASSERT_NE(surface_, nullptr);
    surface_->SetTrackListChange();
    RunUntilShown();
  }

  // Runs until the X-Touch shows the project as it is now.
  void RunUntilShown() {
    surface_->Run();
    surface_->Run();
  }

  // Presses and releases the button, each arriving on a run of its own, then
  // runs until whatever the press started is done, and shown.
  void Tap(Button button) {
    xtouch_.Press(button);
    surface_->Run();
    xtouch_.Release(button);
    Settle();
  }
  void Tap(StripButton button, int strip) {
    xtouch_.Press(button, strip);
    surface_->Run();
    xtouch_.Release(button, strip);
    Settle();
  }

  // Returns the root view's child with the name.
  View* GetView(std::string_view name) const {
    return scene_->GetRootView()->GetChildView(name);
  }

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  SurfaceNotifier notifier_{&reaper_};
  FakeXTouch xtouch_{reaper_, FakeXTouch::Type::kFull, "X-Touch"};
  std::optional<FakeXTouch> xtouch_ext_;
  Runner device_runner_{"Devices"};
  Runner scene_runner_{"Scene"};
  std::optional<MidiPorts> ports_;
  std::unique_ptr<Scene> scene_;
  std::vector<std::string> errors_;
  std::unique_ptr<TestControlSurface> surface_;

 private:
  // Runs until whatever a press started is done, and shown: a press held back
  // in case it is a double press is acted on in the last run of the double
  // press window.
  void Settle() {
    surface_->RunFor(absl::Seconds(Control::kDoublePressWindowSecs));
    RunUntilShown();
  }

  // Runs the scene as PluginSurface does.
  class Listener final : public ControlSurfaceListener {
   public:
    explicit Listener(CreateSceneTest* test) : test_(test) {
      test_->scene_->Activate(test_->scene_runner_);
    }
    ~Listener() override { test_->scene_->Deactivate(); }

    static std::unique_ptr<ControlSurfaceListener> Create(
        std::string_view config) {
      return std::make_unique<Listener>(g_create_scene_test);
    }

    void OnRun(const RunTime& time) override {
      test_->device_runner_.Run(time);
      test_->ports_->RunInput(time);
      test_->scene_runner_.Run(time);
      test_->ports_->RunOutput(time);
    }

   private:
    CreateSceneTest* const test_;
  };
};

TEST_F(CreateSceneTest, ASpecThatIsntValidCreatesNothing) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.root.uses = {{.name = "nothing"}};
  EXPECT_THAT(Create(spec), IsNull());
  EXPECT_THAT(errors_, ElementsAre("/: uses \"nothing\", which isn't a "
                                   "template"));
  EXPECT_FALSE(xtouch_.GetInputPort()->IsOpen());
}

TEST_F(CreateSceneTest, ARequiredDeviceThatIsAbsentCreatesNothing) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.devices.push_back({.name = "xtouch_ext",
                          .type = "XTouchExt",
                          .input_port = "X-Touch-Ext",
                          .output_port = "X-Touch-Ext"});
  EXPECT_THAT(Create(spec), IsNull());
  EXPECT_THAT(errors_, Not(IsEmpty()));
  EXPECT_THAT(errors_, Each(StartsWith("xtouch_ext: ")));
  EXPECT_FALSE(xtouch_.GetInputPort()->IsOpen());
}

TEST_F(CreateSceneTest, AnAbsentDevicesElementsHaveNoInstances) {
  // The strips are the extender's, if it is present, then the X-Touch's.
  SurfaceSpec spec = GetXTouchSpec();
  spec.devices.push_back({.name = "xtouch_ext",
                          .type = "XTouchExt",
                          .input_port = "X-Touch-Ext",
                          .output_port = "X-Touch-Ext",
                          .required = false});
  spec.widgets = {.includes = {{.path = "xtouch", .excluded = {"strip"}}},
                  .fields = {{.name = "strip",
                              .widget = WidgetJoinDefinition{
                                  .slices = {{.path = "xtouch_ext.strip"},
                                             {.path = "xtouch.strip"}}}}}};
  spec.root.children = {ListView("tracks", "state:master_track")};

  ASSERT_THAT(Create(spec), NotNull());
  EXPECT_THAT(errors_, IsEmpty());
  View* tracks = GetView("tracks");
  ASSERT_THAT(tracks, NotNull());
  EXPECT_EQ(tracks->GetChildViewCount(), 8);
  EXPECT_THAT(tracks->GetChildView("strip[7]"), NotNull());

  AddExtender();
  ASSERT_THAT(Create(spec), NotNull());
  EXPECT_THAT(errors_, IsEmpty());
  tracks = GetView("tracks");
  ASSERT_THAT(tracks, NotNull());
  EXPECT_EQ(tracks->GetChildViewCount(), 16);
  EXPECT_THAT(tracks->GetChildView("strip[15]"), NotNull());
}

TEST_F(CreateSceneTest, ACommandREAPERDoesntHaveCreatesNothing) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.root.mappings = {{.property = "cmd:_NOT_INSTALLED",
                         .control = "play",
                         .type = ViewMapping::kReadControl},
                        {.property = std::string(kCmdTransportStop),
                         .control = "stop",
                         .type = ViewMapping::kReadControl,
                         .config = {.condition = ViewCondition::Config{
                                        .property = "cmd:_NOR_THIS"}}}};
  spec.root.children = {{.name = "mode",
                         .config = {.condition = ViewCondition::Config{
                                        .property = "cmd:_NOR_THAT"}}}};
  EXPECT_THAT(Create(spec), IsNull());
  EXPECT_THAT(
      errors_,
      ElementsAre("/: play: command \"cmd:_NOT_INSTALLED\" isn't one REAPER "
                  "has",
                  "/: stop: command \"cmd:_NOR_THIS\" isn't one REAPER has",
                  "/mode: command \"cmd:_NOR_THAT\" isn't one REAPER has"));
}

TEST_F(CreateSceneTest, ErrorsStartWithTheLineOfWhatTheyAreAbout) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.templates = {{.name = "button",
                     .mappings = {{.property = "cmd:_NOT_INSTALLED",
                                   .control = "play",
                                   .type = ViewMapping::kReadControl,
                                   .line = {.number = 4}}},
                     .line = {.number = 3}}};
  spec.root.uses = {{.name = "button", .line = {.number = 9}}};
  spec.root.mappings = {{.property = "cmd:_NOR_THIS",
                         .control = "stop",
                         .type = ViewMapping::kReadControl,
                         .line = {.number = 8}}};
  spec.root.children = {
      {.name = "mode",
       .config = {.condition =
                      ViewCondition::Config{.property = "cmd:_NOR_THAT"}},
       .line = {.number = 10}}};
  EXPECT_THAT(Create(spec), IsNull());
  EXPECT_THAT(errors_,
              ElementsAre("line 4: /: button: play: command "
                          "\"cmd:_NOT_INSTALLED\" isn't one REAPER has",
                          "line 8: /: stop: command \"cmd:_NOR_THIS\" isn't "
                          "one REAPER has",
                          "line 10: /mode: command \"cmd:_NOR_THAT\" isn't "
                          "one REAPER has"));
}

TEST_F(CreateSceneTest, ComponentsAreDeclaredWhereTheyAre) {
  std::vector<FakeTrack*> tracks = reaper_.GetProject().AddTracks(4);
  SurfaceSpec spec = GetXTouchSpec();
  spec.components = {
      ModifierSpec{.name = "mod:hold"},
      EnumeratedValueSpec{
          .name = "user:mode", .values = {"a", "b"}, .initial = "b"},
      TrackReferenceSpec{.name = "user:folder",
                         .fallback = "state:master_track"},
      TrackReferenceSpec{.name = "user:current"},
      TrackPickSpec{.name = "user:pick_selected",
                    .reference = "user:current",
                    .source = "state:selected_track"},
  };
  spec.root.children = {ListView(
      "tracks", "user:folder",
      {.components =
           {TrackAnchorSpec{.name = "user:anchor", .modifier = "mod:hold"},
            TrackPickSpec{.name = "user:pick", .reference = "user:current"}},
       .mappings = {{.property = "user:pick",
                     .control = "strip.select",
                     .type = ViewMapping::kReadControl}}})};

  Scene* scene = Create(spec);
  ASSERT_THAT(scene, NotNull());
  EXPECT_THAT(errors_, IsEmpty());
  EXPECT_NE(scene->GetModifier("mod:hold"), 0);
  ViewProperty* mode = scene->GetProperty("user:mode");
  ASSERT_THAT(mode, NotNull());
  EXPECT_EQ(mode->GetText(), "b");
  EXPECT_THAT(scene->GetTrackReference("user:folder"), NotNull());
  EXPECT_THAT(scene->GetProperty("user:pick_selected"), NotNull());
  View* strip = GetView("tracks")->GetChildView("strip[2]");
  ASSERT_THAT(strip, NotNull());
  EXPECT_THAT(strip->GetProperty("user:anchor"), NotNull());

  // Each strip's pick sets the reference to its own track.
  AddSurface();
  Tap(StripButton::kSelect, 2);
  const Track* current = scene->GetTrackReference("user:current")->GetTrack();
  ASSERT_THAT(current, NotNull());
  EXPECT_EQ(current->GetTrackId(), ToMediaTrack(tracks[2]));
}

TEST_F(CreateSceneTest, RepeatedViewsShowTheirParentsListItems) {
  std::vector<FakeTrack*> tracks = reaper_.GetProject().AddTracks(3);
  SurfaceSpec spec = GetXTouchSpec();
  spec.root.children = {
      ListView("tracks", "state:master_track",
               {.mappings = {{.property = "track:name",
                              .control = "strip.scribble_top",
                              .type = ViewMapping::kWriteControl}}})};

  ASSERT_THAT(Create(spec), NotNull());
  EXPECT_THAT(errors_, IsEmpty());
  AddSurface();
  View* list = GetView("tracks");
  ASSERT_THAT(list, NotNull());
  ASSERT_EQ(list->GetChildViewCount(), 8);
  for (int i = 0; i < 3; ++i) {
    View* item = list->GetChildViewAt(i);
    EXPECT_EQ(item->GetName(), absl::StrCat("strip[", i, "]"));
    EXPECT_EQ(item->GetTrack()->GetTrackId(), ToMediaTrack(tracks[i]));
    EXPECT_EQ(absl::StripTrailingAsciiWhitespace(xtouch_.GetScribble(i, 0)),
              tracks[i]->name);
  }
}

TEST_F(CreateSceneTest, TemplatesAreExpandedWithTheirArguments) {
  std::vector<FakeTrack*> tracks = reaper_.GetProject().AddTracks(3);
  SurfaceSpec spec = GetXTouchSpec();
  spec.templates = {
      {.name = "toggle_button",
       .widget_params = {"button"},
       .property_params = {{.name = "toggle"}},
       .mappings = {{.property = "toggle",
                     .control = "button",
                     .type = ViewMapping::kReadWriteControl}}},
      {.name = "strip_buttons",
       .widget_params = {"buttons"},
       .property_params = {{.name = "mute"}},
       .mappings = {{.property = "mute",
                     .control = "buttons.mute",
                     .type = ViewMapping::kReadWriteControl},
                    {.property = FixedValue{.type = ViewProperty::Type::kToggle,
                                            .value = true},
                     .control = "buttons.rec",
                     .type = ViewMapping::kWriteControl,
                     .config = {.condition =
                                    ViewCondition::Config{.property = "mute"}}},
                    {.property = FixedValue{.type = ViewProperty::Type::kToggle,
                                            .value = true},
                     .control = "buttons.select",
                     .type = ViewMapping::kWriteControl,
                     .config = {.write = {.mode_overrides =
                                              {{"mute", {{true, "blink"}}}}}}}},
       .uses = {{.name = "toggle_button",
                 .widgets = {{"button", "buttons.solo"}},
                 .properties = {{"toggle", "track:ui_solo"}}}}},
  };
  spec.root.children = {
      ListView("tracks", "state:master_track",
               {.uses = {{.name = "strip_buttons",
                          .widgets = {{"buttons", "strip"}},
                          .properties = {{"mute", "track:ui_mute"}}}}})};

  ASSERT_THAT(Create(spec), NotNull());
  EXPECT_THAT(errors_, IsEmpty());
  AddSurface();
  Tap(StripButton::kMute, 1);
  EXPECT_TRUE(tracks[1]->mute);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kMute, 1), Light::kOn);

  // A condition's and a mode override's properties are arguments too.
  EXPECT_EQ(xtouch_.GetLight(StripButton::kRec, 1), Light::kOn);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kRec, 0), Light::kOff);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kSelect, 1), Light::kBlinking);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kSelect, 0), Light::kOn);

  Tap(StripButton::kSolo, 2);
  EXPECT_TRUE(tracks[2]->solo);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kSolo, 2), Light::kOn);
  EXPECT_FALSE(tracks[0]->mute);
  EXPECT_FALSE(tracks[0]->solo);
}

TEST_F(CreateSceneTest, AUseThatGivesNoArgumentGetsTheDefault) {
  std::vector<FakeTrack*> tracks = reaper_.GetProject().AddTracks(3);
  SurfaceSpec spec = GetXTouchSpec();
  spec.templates = {
      {.name = "lit_button",
       .widget_params = {"button"},
       .property_params = {{.name = "toggle", .default_value = "track:ui_mute"},
                           {.name = "light", .default_value = "toggle"}},
       .mappings = {{.property = "toggle",
                     .control = "button",
                     .type = ViewMapping::kReadControl},
                    {.property = "light",
                     .control = "button",
                     .type = ViewMapping::kWriteControl}}},
  };
  spec.root.children = {ListView(
      "tracks", "state:master_track",
      {.uses = {{.name = "lit_button", .widgets = {{"button", "strip.mute"}}},
                {.name = "lit_button",
                 .widgets = {{"button", "strip.solo"}},
                 .properties = {{"toggle", "track:ui_solo"}}},
                {.name = "lit_button",
                 .widgets = {{"button", "strip.select"}},
                 .properties = {{"toggle", "track:ui_solo"},
                                {"light", "track:ui_mute"}}}}})};

  ASSERT_THAT(Create(spec), NotNull());
  EXPECT_THAT(errors_, IsEmpty());
  AddSurface();

  // A default that is a name stands for it.
  Tap(StripButton::kMute, 1);
  EXPECT_TRUE(tracks[1]->mute);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kMute, 1), Light::kOn);

  // A default that is an earlier parameter stands for its argument.
  Tap(StripButton::kSolo, 2);
  EXPECT_TRUE(tracks[2]->solo);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kSolo, 2), Light::kOn);

  // An argument replaces the default.
  Tap(StripButton::kSelect, 0);
  EXPECT_TRUE(tracks[0]->solo);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kSelect, 0), Light::kOff);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kSelect, 1), Light::kOn);
}

TEST_F(CreateSceneTest, FixedValuesShareAConstProperty) {
  SurfaceSpec spec = GetXTouchSpec();
  const FixedValue on = {.type = ViewProperty::Type::kToggle, .value = true};
  const FixedValue off = {.type = ViewProperty::Type::kToggle, .value = false};
  spec.root.mappings = {
      {.property = on, .control = "play", .type = ViewMapping::kWriteControl},
      {.property = on, .control = "stop", .type = ViewMapping::kWriteControl},
      {.property = off,
       .control = "record",
       .type = ViewMapping::kWriteControl}};

  Scene* scene = Create(spec);
  ASSERT_THAT(scene, NotNull());
  EXPECT_THAT(errors_, IsEmpty());
  EXPECT_THAT(scene->GetProperty("const:1"), NotNull());
  EXPECT_THAT(scene->GetProperty("const:2"), NotNull());
  EXPECT_THAT(scene->GetProperty("const:3"), IsNull());
  AddSurface();
  EXPECT_EQ(xtouch_.GetLight(Button::kPlay), Light::kOn);
  EXPECT_EQ(xtouch_.GetLight(Button::kStop), Light::kOn);
  EXPECT_EQ(xtouch_.GetLight(Button::kRecord), Light::kOff);
}

TEST_F(CreateSceneTest, EveryViewIsEnabledAndConditionsDecideWhichAreActive) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.components = {
      EnumeratedValueSpec{.name = "user:mode", .values = {"a", "b"}}};
  spec.root.mappings = {
      {.property = "user:mode",
       .control = "assign_track",
       .type = ViewMapping::kReadControl,
       .config = {.read = {.property_min = std::string("b"),
                           .property_max = std::string("b")}}}};
  spec.root.children = {
      {.name = "b_mode",
       .config = {.condition =
                      ViewCondition::Config{.property = "user:mode",
                                            .value = std::string("b")}},
       .mappings = {{.property = FixedValue{.type = ViewProperty::Type::kToggle,
                                            .value = true},
                     .control = "play",
                     .type = ViewMapping::kWriteControl}}}};

  Scene* scene = Create(spec);
  ASSERT_THAT(scene, NotNull());
  EXPECT_THAT(errors_, IsEmpty());
  View* b_mode = GetView("b_mode");
  ASSERT_THAT(b_mode, NotNull());
  EXPECT_TRUE(scene->GetRootView()->IsEnabled());
  EXPECT_TRUE(b_mode->IsEnabled());

  AddSurface();
  EXPECT_FALSE(b_mode->IsActive());
  EXPECT_EQ(xtouch_.GetLight(Button::kPlay), Light::kOff);
  Tap(Button::kAssignTrack);
  EXPECT_TRUE(b_mode->IsActive());
  EXPECT_EQ(xtouch_.GetLight(Button::kPlay), Light::kOn);
}

}  // namespace
}  // namespace jpr
