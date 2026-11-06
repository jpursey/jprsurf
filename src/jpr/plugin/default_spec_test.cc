// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/default_spec.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/config/surface_config.h"
#include "jpr/device/spec_line.h"
#include "jpr/scene/surface_spec.h"
#include "jpr/scene/view_mapping.h"

namespace jpr {
namespace {

using ::testing::IsEmpty;

// Returns the file's contents, or "" if it can't be read.
std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

// Returns the mapping as the config writes it, such as `strip.mute <->
// track:ui_mute`, with a fixed value as "a fixed value".
std::string Describe(const MappingSpec& mapping) {
  std::string_view arrow = "<-";
  if (mapping.type == ViewMapping::kReadWriteControl) {
    arrow = "<->";
  } else if (mapping.type == ViewMapping::kReadControl) {
    arrow = "->";
  }
  const std::string* property = std::get_if<std::string>(&mapping.property);
  return absl::StrCat(mapping.control, " ", arrow, " ",
                      property != nullptr ? *property : "a fixed value");
}

// Returns the use as the config starts it, such as "use track_strip".
std::string Describe(const TemplateUse& use) {
  return absl::StrCat("use ", use.name);
}

// Expects the file's `parts` (mappings or uses) of what `subject` names to be
// `expected`, in any order, adding a failure for each part one has that the
// other doesn't, so one that differs is a failure for each.
template <typename Part>
void ExpectSameParts(std::string_view subject, std::vector<Part> parts,
                     const std::vector<Part>& expected) {
  for (const Part& part : expected) {
    auto it = std::ranges::find(parts, part);
    if (it == parts.end()) {
      ADD_FAILURE() << subject << ": " << Describe(part)
                    << " is in GetDefaultSpec(), but not the file";
    } else {
      parts.erase(it);
    }
  }
  for (const Part& part : parts) {
    ADD_FAILURE() << GetSpecError(
        {part.line, subject},
        absl::StrCat(Describe(part),
                     " is in the file, but not GetDefaultSpec()"));
  }
}

// Expects the file's template to be `expected`.
void ExpectSameTemplate(const TemplateSpec& spec,
                        const TemplateSpec& expected) {
  const std::string subject = absl::StrCat("template ", expected.name);
  EXPECT_EQ(spec.name, expected.name) << subject;
  EXPECT_EQ(spec.widget_params, expected.widget_params) << subject;
  EXPECT_EQ(spec.property_params, expected.property_params) << subject;
  ExpectSameParts(subject, spec.uses, expected.uses);
  ExpectSameParts(subject, spec.mappings, expected.mappings);
}

// Expects the file's view, whose path is `path`, and its descendants, to be
// `expected`.
void ExpectSameView(std::string_view path, const ViewSpec& view,
                    const ViewSpec& expected) {
  EXPECT_EQ(view.name, expected.name) << path;
  EXPECT_EQ(view.config, expected.config) << path << ": config";
  EXPECT_EQ(view.repeat, expected.repeat) << path << ": repeat";
  EXPECT_EQ(view.components, expected.components) << path << ": components";
  ExpectSameParts(path, view.uses, expected.uses);
  ExpectSameParts(path, view.mappings, expected.mappings);
  ASSERT_EQ(view.children.size(), expected.children.size())
      << path << ": children";
  for (int i = 0; i < std::ssize(view.children); ++i) {
    ExpectSameView(GetViewPath(path, expected.children[i]), view.children[i],
                   expected.children[i]);
  }
}

TEST(DefaultSpecTest, IsValid) {
  std::vector<std::string> errors;
  EXPECT_TRUE(ValidateSurfaceSpec(GetDefaultSpec(), &errors));
  EXPECT_THAT(errors, IsEmpty());
}

TEST(DefaultSpecTest, IsTheConfigFile) {
  const std::string text = ReadFile(JPR_CONFIG_FILE);
  ASSERT_FALSE(text.empty()) << JPR_CONFIG_FILE << " can't be read";
  std::vector<std::string> errors;
  const std::optional<SurfaceSpec> spec = ReadSurfaceConfig(text, &errors);
  EXPECT_THAT(errors, IsEmpty());
  ASSERT_TRUE(spec.has_value());

  const SurfaceSpec expected = GetDefaultSpec();
  EXPECT_EQ(spec->devices, expected.devices) << "devices";
  EXPECT_EQ(spec->widgets, expected.widgets) << "widgets";
  EXPECT_EQ(spec->components, expected.components) << "components";
  EXPECT_EQ(spec->track_filter, expected.track_filter) << "track_filter";
  ASSERT_EQ(spec->templates.size(), expected.templates.size()) << "templates";
  for (int i = 0; i < std::ssize(spec->templates); ++i) {
    ExpectSameTemplate(spec->templates[i], expected.templates[i]);
  }
  ExpectSameView("/", spec->root, expected.root);
}

}  // namespace
}  // namespace jpr
