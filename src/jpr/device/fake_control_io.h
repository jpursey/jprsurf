// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "absl/types/span.h"
#include "jpr/common/color.h"
#include "jpr/common/timeline.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"

namespace jpr {

//==============================================================================
// Fake control inputs and outputs
//
// Inputs a test drives, and outputs that keep what they were last set to, for
// testing controls without a device. Only tests include this.
//==============================================================================

// A press input the test presses and releases.
class FakePressInput final : public ControlPressInput {
 public:
  explicit FakePressInput(bool has_release = true)
      : ControlPressInput(has_release) {}

  using ControlPressInput::Press;
  using ControlPressInput::Release;
};

// A value input the test sets.
class FakeValueInput final : public ControlValueInput {
 public:
  using ControlValueInput::SetValue;
};

// A delta input the test adds to.
class FakeDeltaInput final : public ControlDeltaInput {
 public:
  using ControlDeltaInput::AddDelta;
};

// Each output keeps the value and mode it was last set to, and how many times
// it was set.

class FakeCValueOutput final : public ControlCValueOutput {
 public:
  explicit FakeCValueOutput(int mode_count = 1)
      : ControlCValueOutput(mode_count) {}

  double GetValue() const { return value_; }
  int GetMode() const { return mode_; }
  int GetSetCount() const { return set_count_; }

 protected:
  void OnValueChanged(double value, int mode) override {
    value_ = value;
    mode_ = mode;
    ++set_count_;
  }

 private:
  double value_ = 0.0;
  int mode_ = 0;
  int set_count_ = 0;
};

class FakeDValueOutput final : public ControlDValueOutput {
 public:
  // `max_values` is each mode's highest value.
  explicit FakeDValueOutput(absl::Span<const int> max_values = {1})
      : ControlDValueOutput(max_values) {}

  int GetValue() const { return value_; }
  int GetMode() const { return mode_; }
  int GetSetCount() const { return set_count_; }

 protected:
  void OnValueChanged(int value, int mode) override {
    value_ = value;
    mode_ = mode;
    ++set_count_;
  }

 private:
  int value_ = 0;
  int mode_ = 0;
  int set_count_ = 0;
};

// A timeline position is kept as it was set, and not formatted as text.
class FakeTextOutput final : public ControlTextOutput {
 public:
  explicit FakeTextOutput(int mode_count = 1) : ControlTextOutput(mode_count) {}

  // The text it was last set to, or empty if the last was a position.
  const std::string& GetText() const { return text_; }
  int GetMode() const { return mode_; }

  // The position it was last set to, and its mode, or nullopt if the last was
  // text.
  std::optional<double> GetPosition() const { return position_; }
  TimelineMode GetTimelineMode() const { return timeline_mode_; }

  int GetSetCount() const { return set_count_; }

 protected:
  void OnTimelineTextChanged(TimelinePosition position,
                             TimelineMode mode) override {
    text_.clear();
    position_ = position.GetValue();
    timeline_mode_ = mode;
    ++set_count_;
  }

  void OnTextChanged(std::string_view text, int mode) override {
    text_ = std::string(text);
    mode_ = mode;
    position_.reset();
    ++set_count_;
  }

 private:
  std::string text_;
  int mode_ = 0;
  std::optional<double> position_;
  TimelineMode timeline_mode_ = TimelineMode::kBeats;
  int set_count_ = 0;
};

class FakeColorOutput final : public ControlColorOutput {
 public:
  explicit FakeColorOutput(int mode_count = 1)
      : ControlColorOutput(mode_count) {}

  Color GetColor() const { return color_; }
  int GetMode() const { return mode_; }
  int GetSetCount() const { return set_count_; }

 protected:
  void OnColorChanged(Color color, int mode) override {
    color_ = color;
    mode_ = mode;
    ++set_count_;
  }

 private:
  Color color_ = {0, 0, 0};
  int mode_ = 0;
  int set_count_ = 0;
};

}  // namespace jpr
