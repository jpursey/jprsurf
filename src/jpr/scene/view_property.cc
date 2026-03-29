// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_property.h"

#include <string_view>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

ViewProperty::ViewProperty(std::string_view name, Type type)
    : name_(name), type_(type) {}

void ViewProperty::RegisterFlag(bool* flag) {
  flags_.insert(flag);
  if (flags_.size() == 1) {
    OnRegistered();
  }
}

void ViewProperty::UnregisterFlag(bool* flag) {
  if (flags_.empty()) {
    return;
  }
  flags_.erase(flag);
  if (flags_.empty()) {
    OnUnregistered();
  }
}

void ViewProperty::NotifyChanged() {
  for (bool* flag : flags_) {
    *flag = true;
  }
}

ViewProperty::Value ViewProperty::GetValue() const {
  switch (type_) {
    case Type::kAction:
      return std::monostate{};
    case Type::kToggle:
      return ReadBool();
    case Type::kPan:
    case Type::kVolume:
    case Type::kNormalized:
      return ReadDouble();
    case Type::kText:
      return ReadString();
    case Type::kColor:
      return ReadColor();
    case Type::kTimelinePosition:
      return ReadTimelinePosition();
    case Type::kEnumerated:
      return ReadInt();
  }
  return std::monostate{};
}

bool ViewProperty::GetBool() const {
  switch (type_) {
    case Type::kAction:
      return false;
    case Type::kToggle:
      return ReadBool();
    case Type::kPan:
      return ReadDouble() > 0.0;
    case Type::kVolume:
      return ReadDouble() > 1.0;
    case Type::kNormalized:
      return ReadDouble() > 0.5;
    case Type::kText:
      return !ReadString().empty();
    case Type::kColor: {
      return GetLuminance(ReadColor()) > 0.5;
    }
    case Type::kTimelinePosition:
      return ReadTimelinePosition().GetValue() > 0.0;
    case Type::kEnumerated:
      return ReadInt() > 0;
  }
  return false;
}

double ViewProperty::GetPan() const {
  switch (type_) {
    case Type::kAction:
    case Type::kText:
      return 0.0;
    case Type::kToggle:
      return ReadBool() ? 1.0 : -1.0;
    case Type::kPan:
      return std::clamp(ReadDouble(), -1.0, 1.0);
    case Type::kVolume:
    case Type::kNormalized:
      return std::clamp(ReadDouble(), 0.0, 1.0) * 2.0 - 1.0;
    case Type::kColor: {
      return std::clamp(GetLuminance(ReadColor()), 0.0, 1.0) * 2.0 - 1.0;
    }
    case Type::kTimelinePosition:
      return 0.0;
    case Type::kEnumerated: {
      int max_value = GetMaxValue();
      if (max_value <= 0) {
        return 0.0;
      }
      return static_cast<double>(ReadInt()) / max_value * 2.0 - 1.0;
    }
  }
  return 0.0;
}

double ViewProperty::GetVolume() const {
  switch (type_) {
    case Type::kAction:
    case Type::kText:
      return 0.0;
    case Type::kToggle:
      return ReadBool() ? 1.0 : 0.0;
    case Type::kPan:
      return std::max((ReadDouble() + 1.0) / 2.0, 0.0);
    case Type::kVolume:
    case Type::kNormalized:
      return std::max(ReadDouble(), 0.0);
    case Type::kColor: {
      return std::clamp(GetLuminance(ReadColor()), 0.0, 1.0);
    }
    case Type::kTimelinePosition:
      return 0.0;
    case Type::kEnumerated: {
      int max_value = GetMaxValue();
      if (max_value <= 0) {
        return 0.0;
      }
      return static_cast<double>(ReadInt()) / max_value;
    }
  }
  return 0.0;
}

double ViewProperty::GetNormalized() const {
  switch (type_) {
    case Type::kAction:
    case Type::kText:
      return 0.0;
    case Type::kToggle:
      return ReadBool() ? 1.0 : 0.0;
    case Type::kPan:
      return std::clamp((ReadDouble() + 1.0) / 2.0, 0.0, 1.0);
    case Type::kVolume:
    case Type::kNormalized:
      return std::clamp(ReadDouble(), 0.0, 1.0);
    case Type::kColor: {
      return std::clamp(GetLuminance(ReadColor()), 0.0, 1.0);
    }
    case Type::kTimelinePosition:
      return 0.0;
    case Type::kEnumerated: {
      int max_value = GetMaxValue();
      if (max_value <= 0) {
        return 0.0;
      }
      return static_cast<double>(ReadInt()) / max_value;
    }
  }
  return 0.0;
}

std::string ViewProperty::GetText() const {
  switch (type_) {
    case Type::kAction:
      return "";
    case Type::kToggle:
      return ReadBool() ? "On" : "Off";
    case Type::kPan: {
      char pan_string[64];
      mkpanstr(pan_string, ReadDouble());
      return pan_string;
    }
    case Type::kVolume: {
      char volume_string[64];
      mkvolstr(volume_string, ReadDouble());
      return volume_string;
    }
    case Type::kNormalized:
      return absl::StrCat(ReadDouble());
    case Type::kText:
      return ReadString();
    case Type::kColor: {
      Color color = ReadColor();
      return absl::StrCat("#", absl::Hex(color.r, absl::kZeroPad2),
                          absl::Hex(color.g, absl::kZeroPad2),
                          absl::Hex(color.b, absl::kZeroPad2));
    }
    case Type::kTimelinePosition:
      return ReadTimelinePosition().ToString(GetRulerMode());
    case Type::kEnumerated:
      return absl::StrCat(ReadInt());
  }
  return "";
}

Color ViewProperty::GetColor() const {
  switch (type_) {
    case Type::kAction:
    case Type::kText:
      return {0, 0, 0};
    case Type::kToggle:
      return ReadBool() ? Color{255, 255, 255} : Color{0, 0, 0};
    case Type::kPan: {
      double pan = ReadDouble();
      uint8_t value = static_cast<uint8_t>(
          std::clamp((pan + 1.0) / 2.0 * 255.0, 0.0, 255.0));
      return {value, value, value};
    }
    case Type::kVolume:
    case Type::kNormalized: {
      double volume = ReadDouble();
      uint8_t value =
          static_cast<uint8_t>(std::clamp(volume * 255.0, 0.0, 255.0));
      return {value, value, value};
    }
    case Type::kColor:
      return ReadColor();
    case Type::kTimelinePosition:
      return {0, 0, 0};
    case Type::kEnumerated: {
      int max_value = GetMaxValue();
      if (max_value <= 0) {
        return {0, 0, 0};
      }
      double normalized =
          (max_value <= 0) ? 0.0 : static_cast<double>(ReadInt()) / max_value;
      uint8_t value =
          static_cast<uint8_t>(std::clamp(normalized * 255.0, 0.0, 255.0));
      return {value, value, value};
    }
  }
  return {0, 0, 0};
}

void ViewProperty::SetValue(const Value& value) {
  switch (type_) {
    case Type::kAction:
      if (std::holds_alternative<std::monostate>(value)) {
        TriggerAction();
      }
      break;
    case Type::kToggle:
      if (std::holds_alternative<bool>(value)) {
        WriteBool(std::get<bool>(value));
      }
      break;
    case Type::kPan:
      if (std::holds_alternative<double>(value)) {
        WriteDouble(std::clamp(-1.0, 1.0, std::get<double>(value)));
      }
      break;
    case Type::kVolume:
      if (std::holds_alternative<double>(value)) {
        WriteDouble(std::max(0.0, std::get<double>(value)));
      }
      break;
    case Type::kNormalized:
      if (std::holds_alternative<double>(value)) {
        WriteDouble(std::clamp(0.0, 1.0, std::get<double>(value)));
      }
      break;
    case Type::kText:
      if (std::holds_alternative<std::string>(value)) {
        WriteString(std::get<std::string>(value));
      }
      break;
    case Type::kColor:
      if (std::holds_alternative<Color>(value)) {
        WriteColor(std::get<Color>(value));
      }
      break;
    case Type::kTimelinePosition:
      if (std::holds_alternative<TimelinePosition>(value)) {
        WriteTimelinePosition(std::get<TimelinePosition>(value));
      }
      break;
    case Type::kEnumerated:
      if (std::holds_alternative<int>(value)) {
        WriteInt(std::clamp(std::get<int>(value), 0, GetMaxValue()));
      }
      break;
  }
}

void ViewProperty::SetBool(bool value) {
  switch (type_) {
    case Type::kAction:
      if (value) {
        TriggerAction();
      }
      break;
    case Type::kToggle:
      WriteBool(value);
      break;
    case Type::kPan:
      WriteDouble(value ? 1.0 : -1.0);
      break;
    case Type::kVolume:
    case Type::kNormalized:
      WriteDouble(value ? 1.0 : 0.0);
      break;
    case Type::kText:
      WriteString(value ? "On" : "Off");
      break;
    case Type::kColor:
      WriteColor(value ? Color{255, 255, 255} : Color{0, 0, 0});
      break;
    case Type::kTimelinePosition:
      break;
    case Type::kEnumerated:
      WriteInt(value ? GetMaxValue() : 0);
      break;
  }
}

void ViewProperty::SetPan(double value) {
  switch (type_) {
    case Type::kAction:
      break;
    case Type::kToggle:
      WriteBool(value > 0.0);
      break;
    case Type::kPan:
      WriteDouble(std::clamp(value, -1.0, 1.0));
      break;
    case Type::kVolume:
    case Type::kNormalized:
      WriteDouble(std::clamp((value + 1.0) / 2.0, 0.0, 1.0));
      break;
    case Type::kText: {
      char pan_string[64];
      mkpanstr(pan_string, value);
      WriteString(pan_string);
      break;
    }
    case Type::kColor: {
      uint8_t color_value = static_cast<uint8_t>(
          std::clamp((value + 1.0) / 2.0 * 255.0, 0.0, 255.0));
      WriteColor({color_value, color_value, color_value});
      break;
    }
    case Type::kTimelinePosition:
      break;
    case Type::kEnumerated: {
      int max_value = GetMaxValue();
      double normalized = std::clamp((value + 1.0) / 2.0, 0.0, 1.0);
      WriteInt(std::clamp(static_cast<int>(normalized * max_value + 0.5), 0,
                          max_value));
      break;
    }
  }
}

void ViewProperty::SetVolume(double value) {
  switch (type_) {
    case Type::kAction:
      break;
    case Type::kToggle:
      WriteBool(value > 1.0);
      break;
    case Type::kPan:
      WriteDouble(std::clamp(value, 0.0, 1.0) * 2.0 - 1.0);
      break;
    case Type::kVolume:
      WriteDouble(std::max(value, 0.0));
      break;
    case Type::kNormalized:
      WriteDouble(std::clamp(value, 0.0, 1.0));
      break;
    case Type::kText: {
      char volume_string[64];
      mkvolstr(volume_string, value);
      WriteString(volume_string);
      break;
    }
    case Type::kColor: {
      uint8_t color_value =
          static_cast<uint8_t>(std::clamp(value * 255.0, 0.0, 255.0));
      WriteColor({color_value, color_value, color_value});
      break;
    }
    case Type::kTimelinePosition:
      break;
    case Type::kEnumerated: {
      int max_value = GetMaxValue();
      WriteInt(std::clamp(
          static_cast<int>(std::clamp(value, 0.0, 1.0) * max_value + 0.5), 0,
          max_value));
      break;
    }
  }
}

void ViewProperty::SetNormalized(double value) {
  switch (type_) {
    case Type::kAction:
      break;
    case Type::kToggle:
      WriteBool(value > 0.5);
      break;
    case Type::kPan:
      WriteDouble(std::clamp(value, 0.0, 1.0) * 2.0 - 1.0);
      break;
    case Type::kVolume:
      WriteDouble(std::max(value, 0.0));
      break;
    case Type::kNormalized:
      WriteDouble(std::clamp(value, 0.0, 1.0));
      break;
    case Type::kText: {
      WriteString(absl::StrCat(value));
      break;
    }
    case Type::kColor: {
      uint8_t color_value =
          static_cast<uint8_t>(std::clamp(value * 255.0, 0.0, 255.0));
      WriteColor({color_value, color_value, color_value});
      break;
    }
    case Type::kTimelinePosition:
      break;
    case Type::kEnumerated: {
      int max_value = GetMaxValue();
      WriteInt(std::clamp(
          static_cast<int>(std::clamp(value, 0.0, 1.0) * max_value + 0.5), 0,
          max_value));
      break;
    }
  }
}

void ViewProperty::SetText(std::string_view value) {
  switch (type_) {
    case Type::kAction:
      break;
    case Type::kToggle:
      WriteBool(!value.empty());
      break;
    case Type::kPan: {
      double pan_value = 0.0;
      absl::SimpleAtod(value, &pan_value);
      WriteDouble(std::clamp(pan_value, -1.0, 1.0));
      break;
    }
    case Type::kVolume: {
      double volume_value = 0.0;
      absl::SimpleAtod(value, &volume_value);
      WriteDouble(std::max(volume_value, 0.0));
      break;
    }
    case Type::kNormalized: {
      double normalized_value = 0.0;
      absl::SimpleAtod(value, &normalized_value);
      WriteDouble(std::clamp(normalized_value, 0.0, 1.0));
      break;
    }
    case Type::kText:
      WriteString(value);
      break;
    case Type::kColor:
      if (value.size() == 7 && value[0] == '#') {
        uint8_t r = std::stoi(std::string(value.substr(1, 2)), nullptr, 16);
        uint8_t g = std::stoi(std::string(value.substr(3, 2)), nullptr, 16);
        uint8_t b = std::stoi(std::string(value.substr(5, 2)), nullptr, 16);
        WriteColor({r, g, b});
      }
      break;
    case Type::kTimelinePosition:
      break;
    case Type::kEnumerated: {
      int int_value = 0;
      absl::SimpleAtoi(value, &int_value);
      WriteInt(std::clamp(int_value, 0, GetMaxValue()));
      break;
    }
  }
}

void ViewProperty::SetColor(const Color& value) {
  switch (type_) {
    case Type::kAction:
      break;
    case Type::kToggle:
      WriteBool(GetLuminance(value) > 0.5);
      break;
    case Type::kPan:
      WriteDouble(std::clamp(GetLuminance(value), 0.0, 1.0) * 2.0 - 1.0);
      break;
    case Type::kVolume:
    case Type::kNormalized:
      WriteDouble(std::clamp(GetLuminance(value), 0.0, 1.0));
      break;
    case Type::kText: {
      WriteString(absl::StrCat("#", absl::Hex(value.r, absl::kZeroPad2),
                               absl::Hex(value.g, absl::kZeroPad2),
                               absl::Hex(value.b, absl::kZeroPad2)));
      break;
    }
    case Type::kColor:
      WriteColor(value);
      break;
    case Type::kTimelinePosition:
      break;
    case Type::kEnumerated: {
      int max_value = GetMaxValue();
      double luminance = std::clamp(GetLuminance(value), 0.0, 1.0);
      WriteInt(std::clamp(static_cast<int>(luminance * max_value + 0.5), 0,
                          max_value));
      break;
    }
  }
}

TimelinePosition ViewProperty::GetTimelinePosition() const {
  switch (type_) {
    case Type::kAction:
    case Type::kToggle:
    case Type::kPan:
    case Type::kVolume:
    case Type::kNormalized:
    case Type::kText:
    case Type::kColor:
    case Type::kEnumerated:
      return {};
    case Type::kTimelinePosition:
      return ReadTimelinePosition();
  }
  return {};
}

void ViewProperty::SetTimelinePosition(TimelinePosition value) {
  switch (type_) {
    case Type::kAction:
    case Type::kToggle:
    case Type::kPan:
    case Type::kVolume:
    case Type::kNormalized:
    case Type::kText:
    case Type::kColor:
    case Type::kEnumerated:
      break;
    case Type::kTimelinePosition:
      WriteTimelinePosition(value);
      break;
  }
}

int ViewProperty::GetInt() const {
  switch (type_) {
    case Type::kAction:
      return 0;
    case Type::kToggle:
      return ReadBool() ? GetMaxValue() : 0;
    case Type::kPan:
      return static_cast<int>(std::clamp((ReadDouble() + 1.0) / 2.0, 0.0, 1.0) *
                                  GetMaxValue() +
                              0.5);
    case Type::kVolume:
    case Type::kNormalized:
      return static_cast<int>(
          std::clamp(ReadDouble(), 0.0, 1.0) * GetMaxValue() + 0.5);
    case Type::kText: {
      int int_value = 0;
      absl::SimpleAtoi(ReadString(), &int_value);
      return std::clamp(int_value, 0, GetMaxValue());
    }
    case Type::kColor: {
      double luminance = std::clamp(GetLuminance(ReadColor()), 0.0, 1.0);
      return static_cast<int>(luminance * GetMaxValue() + 0.5);
    }
    case Type::kTimelinePosition:
      return 0;
    case Type::kEnumerated:
      return ReadInt();
  }
  return 0;
}

void ViewProperty::SetInt(int value) {
  switch (type_) {
    case Type::kAction:
      if (value > 0) {
        TriggerAction();
      }
      break;
    case Type::kToggle:
      WriteBool(value > 0);
      break;
    case Type::kPan: {
      int max_value = GetMaxValue();
      if (max_value <= 0) {
        WriteDouble(0.0);
        break;
      }
      double pan = static_cast<double>(value) / max_value * 2.0 - 1.0;
      WriteDouble(std::clamp(pan, -1.0, 1.0));
      break;
    }
    case Type::kVolume: {
      int max_value = GetMaxValue();
      if (max_value <= 0) {
        WriteDouble(0.0);
        break;
      }
      double volume = static_cast<double>(value) / max_value;
      WriteDouble(std::max(volume, 0.0));
      break;
    }
    case Type::kNormalized: {
      int max_value = GetMaxValue();
      if (max_value <= 0) {
        WriteDouble(0.0);
        break;
      }
      double normalized = static_cast<double>(value) / max_value;
      WriteDouble(std::clamp(normalized, 0.0, 1.0));
      break;
    }
    case Type::kText:
      WriteString(absl::StrCat(value));
      break;
    case Type::kColor: {
      int max_value = GetMaxValue();
      if (max_value <= 0) {
        WriteColor({0, 0, 0});
        break;
      }
      double normalized = static_cast<double>(value) / max_value;
      uint8_t color_value =
          static_cast<uint8_t>(std::clamp(normalized * 255.0, 0.0, 255.0));
      WriteColor({color_value, color_value, color_value});
      break;
    }
    case Type::kTimelinePosition:
      break;
    case Type::kEnumerated:
      WriteInt(std::clamp(value, 0, GetMaxValue()));
      break;
  }
}

}  // namespace jpr