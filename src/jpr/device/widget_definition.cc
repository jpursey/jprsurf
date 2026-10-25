// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/widget_definition.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "jpr/device/widget_path.h"

namespace jpr {

namespace {

// Returns the path of a field of the struct at `struct_path`, which is empty
// for the top-level struct.
std::string GetFieldPath(std::string_view struct_path,
                         std::string_view field_name) {
  return struct_path.empty() ? std::string(field_name)
                             : absl::StrCat(struct_path, ".", field_name);
}

// Returns the slice as errors describe it, such as `slice "xtouch.strip" from
// 6 for 2`.
std::string GetSliceText(const WidgetSlice& slice) {
  std::string text =
      absl::StrCat("slice \"", slice.path, "\" from ", slice.first);
  if (slice.count.has_value()) {
    absl::StrAppend(&text, " for ", *slice.count);
  }
  return text;
}

// Returns the device's instance of the shape at `path` in its catalog: its
// controls are the device's controls named by their catalog paths, or null if
// the device is null (absent) or has no such control. An absent device's
// arrays are empty, and its structs keep their fields, so `path` is unused.
std::unique_ptr<const Widget> Instantiate(Device* device,
                                          const WidgetShape& shape,
                                          const std::string& path) {
  switch (shape.GetKind()) {
    case WidgetKind::kControl:
      return Widget::ForControl(device != nullptr ? device->GetControl(path)
                                                  : nullptr);
    case WidgetKind::kStruct: {
      std::vector<Widget::Field> fields;
      for (const WidgetShape::Field& field : shape.GetFields()) {
        fields.push_back(
            {field.name,
             Instantiate(device, field.shape,
                         device != nullptr ? GetFieldPath(path, field.name)
                                           : std::string())});
      }
      return Widget::ForStruct(std::move(fields));
    }
    case WidgetKind::kArray: {
      std::vector<std::unique_ptr<const Widget>> elements;
      if (device != nullptr) {
        for (int i = 0; i < shape.GetSize(); ++i) {
          elements.push_back(Instantiate(device, *shape.GetElement(),
                                         absl::StrCat(path, "[", i, "]")));
        }
      }
      return Widget::ForArray(std::move(elements));
    }
  }
  return Widget::ForArray({});
}

// A widget checked so far: its shape, and the widget it resolves to on the
// devices present.
struct Checked {
  WidgetShape shape;
  std::unique_ptr<const Widget> widget;
};

// A widget a path names in a device's catalog: its shape, the device it
// resolves on (or null if it is absent), and its path in the catalog.
struct Found {
  const WidgetShape* shape = nullptr;
  Device* device = nullptr;
  std::string path;
};

// Checks and resolves the widgets of one call to CheckWidgets() or
// ResolveWidgets(). Each error's subject is what it is about: a device and a
// path in its catalog, or the path of a struct or field being defined. Both
// are empty at the top.
class WidgetChecker {
 public:
  // Resolves on the devices present if `resolve` is set, or else as if every
  // device were absent.
  WidgetChecker(bool resolve, std::vector<std::string>* errors)
      : resolve_(resolve), errors_(errors) {}

  std::optional<Checked> Check(const WidgetStructDefinition& widgets,
                               absl::Span<const WidgetDevice> devices);

 private:
  // The fields of a struct being checked, and the name of every field defined,
  // including those that didn't check, so a name defined twice is reported
  // either way.
  struct StructFields {
    std::vector<WidgetShape::Field> shape_fields;
    std::vector<Widget::Field> widget_fields;
    absl::flat_hash_set<std::string_view> names;
  };

  void CheckDevices(absl::Span<const WidgetDevice> devices);
  void CheckCatalog(std::string_view device_name, const WidgetShape& shape,
                    const std::string& path);
  Checked CheckStruct(const WidgetStructDefinition& definition,
                      const std::string& struct_path);
  void CheckInclude(const WidgetInclude& include,
                    const std::string& struct_path, StructFields& fields);
  void AddField(std::string_view struct_path, std::string_view name,
                std::optional<Checked> checked, StructFields& fields);
  std::optional<Checked> CheckField(const WidgetFieldDefinition& definition,
                                    const std::string& field_path);
  std::optional<Checked> CheckJoin(const WidgetJoinDefinition& definition,
                                   const std::string& field_path);

  // Returns the widget at the path's text, or nullopt with an error. `what` is
  // what the path is for in errors, such as "include".
  std::optional<Found> Find(std::string_view text, std::string_view what,
                            std::string_view subject);

  // Adds an error if the name isn't a widget name. `what` is what it names in
  // errors, such as "field".
  void CheckWidgetName(std::string_view subject, std::string_view what,
                       std::string_view name);

  // Adds the name to `names`, or returns false with an error if it is already
  // there.
  bool AddName(std::string_view subject, std::string_view what,
               std::string_view name,
               absl::flat_hash_set<std::string_view>& names);

  void AddError(std::string_view subject, std::string_view message);

  const bool resolve_;
  std::vector<std::string>* errors_;

  // The first device of each name, or null if it has no struct catalog, which
  // CheckDevices() reported, so paths from it aren't reported again.
  absl::flat_hash_map<std::string_view, const WidgetDevice*> devices_;

  bool failed_ = false;
};

std::optional<Checked> WidgetChecker::Check(
    const WidgetStructDefinition& widgets,
    absl::Span<const WidgetDevice> devices) {
  CheckDevices(devices);
  Checked checked = CheckStruct(widgets, "");
  if (failed_) {
    return std::nullopt;
  }
  return checked;
}

void WidgetChecker::CheckDevices(absl::Span<const WidgetDevice> devices) {
  absl::flat_hash_set<const WidgetShape*> catalogs;
  for (const WidgetDevice& device : devices) {
    CheckWidgetName("", "device", device.name);
    auto [it, is_new] = devices_.try_emplace(device.name, nullptr);
    if (!is_new) {
      AddError("", absl::StrCat("device \"", device.name, "\" is repeated"));
    }
    if (device.catalog == nullptr) {
      AddError("", absl::StrCat("device \"", device.name, "\" has no catalog"));
      continue;
    }
    if (device.catalog->GetKind() != WidgetKind::kStruct) {
      AddError("", absl::StrCat("catalog of device \"", device.name,
                                "\" isn't a struct"));
      continue;
    }
    if (is_new) {
      it->second = &device;
    }
    // Devices sharing a catalog would only repeat its errors.
    if (catalogs.insert(device.catalog).second) {
      CheckCatalog(device.name, *device.catalog, "");
    }
  }
}

void WidgetChecker::CheckCatalog(std::string_view device_name,
                                 const WidgetShape& shape,
                                 const std::string& path) {
  const std::string subject = path.empty()
                                  ? std::string(device_name)
                                  : absl::StrCat(device_name, ": ", path);
  absl::flat_hash_set<std::string_view> names;
  switch (shape.GetKind()) {
    case WidgetKind::kControl:
      for (const std::string& mode_name : shape.GetModeNames()) {
        CheckWidgetName(subject, "mode", mode_name);
        AddName(subject, "mode", mode_name, names);
      }
      break;
    case WidgetKind::kStruct:
      for (const WidgetShape::Field& field : shape.GetFields()) {
        CheckWidgetName(subject, "field", field.name);
        AddName(subject, "field", field.name, names);
        CheckCatalog(device_name, field.shape, GetFieldPath(path, field.name));
      }
      break;
    case WidgetKind::kArray:
      // Every element has the same shape, so the first stands for them all.
      CheckCatalog(device_name, *shape.GetElement(), absl::StrCat(path, "[0]"));
      break;
  }
}

Checked WidgetChecker::CheckStruct(const WidgetStructDefinition& definition,
                                   const std::string& struct_path) {
  StructFields fields;
  for (const WidgetInclude& include : definition.includes) {
    CheckInclude(include, struct_path, fields);
  }
  for (const WidgetFieldDefinition& field : definition.fields) {
    CheckWidgetName(struct_path, "field", field.name);
    AddField(struct_path, field.name,
             CheckField(field, GetFieldPath(struct_path, field.name)), fields);
  }
  return {WidgetShape::ForStruct(std::move(fields.shape_fields)),
          Widget::ForStruct(std::move(fields.widget_fields))};
}

void WidgetChecker::CheckInclude(const WidgetInclude& include,
                                 const std::string& struct_path,
                                 StructFields& fields) {
  std::optional<Found> included = Find(include.path, "include", struct_path);
  if (!included.has_value()) {
    return;
  }
  const WidgetShape& shape = *included->shape;
  if (shape.GetKind() != WidgetKind::kStruct) {
    AddError(struct_path,
             absl::StrCat("include \"", include.path, "\" isn't a struct"));
    return;
  }
  for (const std::string& excluded : include.excluded) {
    if (shape.GetField(excluded) == nullptr) {
      AddError(struct_path,
               absl::StrCat("include \"", include.path, "\" excludes \"",
                            excluded, "\", which it doesn't have"));
    }
  }
  for (const WidgetShape::Field& field : shape.GetFields()) {
    // A field repeated in a catalog is the first of its name, and checking the
    // catalog reported the rest.
    if (shape.GetField(field.name) == &field.shape &&
        !absl::c_linear_search(include.excluded, field.name)) {
      AddField(struct_path, field.name,
               Checked{field.shape,
                       Instantiate(included->device, field.shape,
                                   GetFieldPath(included->path, field.name))},
               fields);
    }
  }
}

void WidgetChecker::AddField(std::string_view struct_path,
                             std::string_view name,
                             std::optional<Checked> checked,
                             StructFields& fields) {
  if (AddName(struct_path, "field", name, fields.names) &&
      checked.has_value()) {
    fields.shape_fields.push_back(
        {std::string(name), std::move(checked->shape)});
    fields.widget_fields.push_back(
        {std::string(name), std::move(checked->widget)});
  }
}

std::optional<Checked> WidgetChecker::CheckField(
    const WidgetFieldDefinition& definition, const std::string& field_path) {
  if (const std::string* path = std::get_if<std::string>(&definition.widget)) {
    std::optional<Found> found = Find(*path, "path", field_path);
    if (!found.has_value()) {
      return std::nullopt;
    }
    return Checked{*found->shape,
                   Instantiate(found->device, *found->shape, found->path)};
  }
  if (const WidgetStructDefinition* struct_definition =
          std::get_if<WidgetStructDefinition>(&definition.widget)) {
    return CheckStruct(*struct_definition, field_path);
  }
  if (const WidgetJoinDefinition* join_definition =
          std::get_if<WidgetJoinDefinition>(&definition.widget)) {
    return CheckJoin(*join_definition, field_path);
  }
  return std::nullopt;
}

std::optional<Checked> WidgetChecker::CheckJoin(
    const WidgetJoinDefinition& definition, const std::string& field_path) {
  if (definition.slices.empty()) {
    AddError(field_path, "join has no slices");
    return std::nullopt;
  }
  const WidgetShape* element = nullptr;
  const WidgetSlice* element_slice = nullptr;
  int64_t size = 0;
  std::vector<std::unique_ptr<const Widget>> elements;
  for (const WidgetSlice& slice : definition.slices) {
    std::optional<Found> found = Find(slice.path, "slice", field_path);
    if (!found.has_value()) {
      continue;
    }
    const WidgetShape& array = *found->shape;
    if (array.GetKind() != WidgetKind::kArray) {
      AddError(field_path,
               absl::StrCat(GetSliceText(slice), " isn't an array"));
      continue;
    }
    const int array_size = array.GetSize();
    if (slice.first < 0 || slice.first > array_size ||
        (slice.count.has_value() &&
         int64_t{slice.first} + *slice.count > array_size)) {
      AddError(field_path, absl::StrCat(GetSliceText(slice), " runs past its ",
                                        array_size, " elements"));
      continue;
    }
    const int count = slice.count.value_or(array_size - slice.first);
    if (count <= 0) {
      AddError(field_path, absl::StrCat(GetSliceText(slice), " is empty"));
      continue;
    }
    if (element == nullptr) {
      element = array.GetElement();
      element_slice = &slice;
    } else if (*array.GetElement() != *element) {
      AddError(field_path, absl::StrCat(GetSliceText(slice),
                                        " has elements of another shape than ",
                                        GetSliceText(*element_slice)));
      continue;
    }
    size += count;
    // An absent device's array has no elements, so the slice has none.
    if (found->device != nullptr) {
      for (int i = slice.first; i < slice.first + count; ++i) {
        elements.push_back(Instantiate(found->device, *array.GetElement(),
                                       absl::StrCat(found->path, "[", i, "]")));
      }
    }
  }
  if (element == nullptr) {
    return std::nullopt;
  }
  if (size > std::numeric_limits<int>::max()) {
    AddError(field_path,
             absl::StrCat("join has ", size, " elements, more than ",
                          std::numeric_limits<int>::max()));
    return std::nullopt;
  }
  return Checked{WidgetShape::ForArray(*element, static_cast<int>(size)),
                 Widget::ForArray(std::move(elements))};
}

std::optional<Found> WidgetChecker::Find(std::string_view text,
                                         std::string_view what,
                                         std::string_view subject) {
  std::string error_message;
  std::optional<WidgetPath> path = WidgetPath::Parse(text, &error_message);
  if (!path.has_value()) {
    AddError(subject, absl::StrCat(what, " \"", text,
                                   "\" isn't a path: ", error_message));
    return std::nullopt;
  }
  const std::string* device_name =
      path->GetSteps().empty()
          ? nullptr
          : std::get_if<std::string>(&path->GetSteps().front());
  auto it =
      device_name != nullptr ? devices_.find(*device_name) : devices_.end();
  if (it == devices_.end()) {
    AddError(subject,
             absl::StrCat(what, " \"", text, "\" doesn't start at a device"));
    return std::nullopt;
  }
  const WidgetDevice* device = it->second;
  if (device == nullptr) {
    return std::nullopt;
  }
  const WidgetPath catalog_path = path->GetSubpath(1);
  const WidgetShape* shape = device->catalog->Find(catalog_path);
  if (shape == nullptr) {
    AddError(subject, absl::StrCat(what, " \"", text, "\" names nothing"));
    return std::nullopt;
  }
  return Found{shape, resolve_ ? device->device : nullptr,
               catalog_path.ToString()};
}

void WidgetChecker::CheckWidgetName(std::string_view subject,
                                    std::string_view what,
                                    std::string_view name) {
  if (!IsWidgetName(name)) {
    AddError(subject,
             absl::StrCat(what, " name \"", name, "\" isn't a widget name"));
  }
}

bool WidgetChecker::AddName(std::string_view subject, std::string_view what,
                            std::string_view name,
                            absl::flat_hash_set<std::string_view>& names) {
  if (!names.insert(name).second) {
    AddError(subject, absl::StrCat(what, " \"", name, "\" is repeated"));
    return false;
  }
  return true;
}

void WidgetChecker::AddError(std::string_view subject,
                             std::string_view message) {
  failed_ = true;
  if (errors_ != nullptr) {
    errors_->push_back(subject.empty() ? std::string(message)
                                       : absl::StrCat(subject, ": ", message));
  }
}

}  // namespace

std::optional<WidgetShape> CheckWidgets(const WidgetStructDefinition& widgets,
                                        absl::Span<const WidgetDevice> devices,
                                        std::vector<std::string>* errors) {
  std::optional<Checked> checked =
      WidgetChecker(/*resolve=*/false, errors).Check(widgets, devices);
  if (!checked.has_value()) {
    return std::nullopt;
  }
  return std::move(checked->shape);
}

std::unique_ptr<const Widget> ResolveWidgets(
    const WidgetStructDefinition& widgets,
    absl::Span<const WidgetDevice> devices, std::vector<std::string>* errors) {
  std::optional<Checked> checked =
      WidgetChecker(/*resolve=*/true, errors).Check(widgets, devices);
  if (!checked.has_value()) {
    return nullptr;
  }
  return std::move(checked->widget);
}

}  // namespace jpr
