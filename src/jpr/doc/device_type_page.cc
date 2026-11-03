// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/doc/device_type_page.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/widget_doc.h"
#include "jpr/device/widget_path.h"
#include "jpr/device/widget_shape.h"
#include "jpr/doc/markdown.h"

namespace jpr {

namespace {

// The page of one device type.
class PageWriter {
 public:
  explicit PageWriter(const DeviceType& type) : type_(type) {}

  // Returns the page.
  std::string Write();

 private:
  // A set of modes that controls share, as one array in their docs.
  struct ModeSet {
    absl::Span<const WidgetDoc::Mode> modes;
    std::vector<std::string> widgets;  // By element path.
  };

  // Adds a row for each field of the struct, and for the widgets in it. The
  // struct is at the path as it is listed (empty for the catalog), with the
  // index of each array it is in named from i, and at the doc path. `depth`
  // is the number of arrays it is in.
  void AddFields(const WidgetShape& shape, const std::string& path,
                 const std::string& doc_path, int depth);

  // Adds the row of the field, which is at the paths, and its modes to the
  // set it shares. An array is listed once, with its range, and its element
  // is documented with it.
  void AddField(const WidgetShape& shape, const std::string& path,
                const std::string& doc_path, int depth);

  // Adds the modes of the widget, at the element path, to the set of them.
  void AddModes(absl::Span<const WidgetDoc::Mode> modes,
                const std::string& path);

  // Appends a table of the set's modes to the page.
  static void AppendModeSet(const ModeSet& set, std::string& page);

  const DeviceType& type_;
  std::string rows_;
  std::vector<ModeSet> mode_sets_;
};

std::string PageWriter::Write() {
  AddFields(*type_.catalog, "", "", 0);

  std::string page = absl::StrCat(
      GetGeneratedPageHeader(absl::StrCat("the ", type_.name, " catalog")),
      "\n# ", type_.name, "\n\n", type_.catalog_doc->description,
      "\n\n## Widgets\n\n"
      "| Widget | Inputs | Outputs | Modes | Description |\n"
      "| --- | --- | --- | --- | --- |\n",
      rows_);
  if (!mode_sets_.empty()) {
    absl::StrAppend(&page, "\n## Modes\n");
  }
  for (const ModeSet& set : mode_sets_) {
    AppendModeSet(set, page);
  }
  return page;
}

void PageWriter::AddFields(const WidgetShape& shape, const std::string& path,
                           const std::string& doc_path, int depth) {
  for (const WidgetShape::Field& field : shape.GetFields()) {
    AddField(field.shape, GetFieldPathText(path, field.name),
             GetFieldPathText(doc_path, field.name), depth);
  }
}

void PageWriter::AddField(const WidgetShape& shape, const std::string& path,
                          const std::string& doc_path, int depth) {
  std::string listed_path = path;
  std::string element_path = path;
  const WidgetShape* widget = &shape;
  while (widget->GetKind() == WidgetKind::kArray) {
    if (widget->GetSize() == 0) {
      absl::StrAppend(&listed_path, "[]");
    } else {
      absl::StrAppend(&listed_path, "[0..", widget->GetSize() - 1, "]");
    }
    absl::StrAppend(&element_path, "[", std::string(1, 'i' + depth++), "]");
    widget = widget->GetElement();
  }

  const WidgetDoc* doc = type_.catalog_doc->FindWidget(doc_path);
  std::string inputs;
  std::string outputs;
  if (widget->GetKind() == WidgetKind::kControl) {
    inputs = GetFlagNames(widget->GetInputs(), ControlInput::kTypes,
                          &ControlInput::GetTypeName);
    outputs = GetFlagNames(widget->GetOutputs(), ControlOutput::kTypes,
                           &ControlOutput::GetTypeName);
  }
  if (doc != nullptr && !doc->modes.empty()) {
    AddModes(doc->modes, element_path);
  }
  absl::StrAppend(&rows_, "| `", listed_path, "` | ", inputs, " | ", outputs,
                  " | ", GetCodeList(widget->GetModeNames()), " | ",
                  GetCellText(doc != nullptr ? doc->description : ""), " |\n");
  AddFields(*widget, element_path, doc_path, depth);
}

void PageWriter::AddModes(absl::Span<const WidgetDoc::Mode> modes,
                          const std::string& path) {
  for (ModeSet& set : mode_sets_) {
    if (set.modes.data() == modes.data() && set.modes.size() == modes.size()) {
      set.widgets.push_back(path);
      return;
    }
  }
  mode_sets_.push_back({.modes = modes, .widgets = {path}});
}

void PageWriter::AppendModeSet(const ModeSet& set, std::string& page) {
  std::vector<std::string_view> names;
  for (const WidgetDoc::Mode& mode : set.modes) {
    names.push_back(mode.name);
  }
  absl::StrAppend(&page, "\n### ", GetCodeList(names), "\n\nThe modes of ",
                  GetCodeList(set.widgets),
                  ".\n\n| Mode | Description |\n| --- | --- |\n");
  for (const WidgetDoc::Mode& mode : set.modes) {
    absl::StrAppend(&page, "| `", mode.name, "` | ",
                    GetCellText(mode.description), " |\n");
  }
}

}  // namespace

std::string GenerateDeviceTypePage(const DeviceType& type) {
  return PageWriter(type).Write();
}

std::string GetDeviceTypePageName(const DeviceType& type) {
  return absl::StrCat(type.name, ".md");
}

std::string GenerateDeviceTypesIndexPage(absl::Span<const DeviceType> types) {
  std::string page = absl::StrCat(
      GetGeneratedPageHeader("the device types"),
      "\n# Device types\n\nThe hardware JPRSurf supports, each with a page of "
      "its widgets, by the names JPRSurf gives them.\n\n"
      "| Device type | Description |\n| --- | --- |\n");
  for (const DeviceType& type : types) {
    absl::StrAppend(&page, "| [", type.name, "](", GetDeviceTypePageName(type),
                    ") | ", GetCellText(type.catalog_doc->description), " |\n");
  }
  return page;
}

}  // namespace jpr
