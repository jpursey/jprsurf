// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/widget_definition.h"

#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include "absl/algorithm/container.h"
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

// Checks the widgets of one call to CheckWidgets(). Each error's subject is
// what it is about: a device and a path in its catalog, or the path of a
// struct or field being defined. Both are empty at the top.
class WidgetChecker {
 public:
  explicit WidgetChecker(std::vector<std::string>* errors) : errors_(errors) {}

  std::optional<WidgetShape> Check(const WidgetStructDefinition& widgets,
                                   absl::Span<const WidgetDevice> devices);

 private:
  // The fields of a struct being checked, and the name of every field defined,
  // including those that didn't check, so a name defined twice is reported
  // either way.
  struct StructFields {
    std::vector<WidgetShape::Field> fields;
    absl::flat_hash_set<std::string_view> names;
  };

  void CheckDevices(absl::Span<const WidgetDevice> devices);
  void CheckCatalog(std::string_view device_name, const WidgetShape& shape,
                    const std::string& path);
  WidgetShape CheckStruct(const WidgetStructDefinition& definition,
                          const std::string& struct_path);
  void CheckInclude(const WidgetInclude& include,
                    const std::string& struct_path, StructFields& fields);
  void AddField(std::string_view struct_path, std::string_view name,
                std::optional<WidgetShape> shape, StructFields& fields);
  std::optional<WidgetShape> CheckField(const WidgetFieldDefinition& definition,
                                        const std::string& field_path);
  std::optional<WidgetShape> CheckJoin(const WidgetJoinDefinition& definition,
                                       const std::string& field_path);

  // Returns the shape at the path's text, or null with an error. `what` is
  // what the path is for in errors, such as "include".
  const WidgetShape* FindShape(std::string_view text, std::string_view what,
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

  std::vector<std::string>* errors_;

  // The catalogs of the devices that checked, as fields named by the devices,
  // so a path from a device is a path from this.
  WidgetShape devices_ = WidgetShape::ForStruct({});

  // The devices without a struct catalog, which CheckDevices() reported, so
  // paths from them aren't reported again.
  absl::flat_hash_set<std::string_view> unusable_devices_;

  bool failed_ = false;
};

std::optional<WidgetShape> WidgetChecker::Check(
    const WidgetStructDefinition& widgets,
    absl::Span<const WidgetDevice> devices) {
  CheckDevices(devices);
  WidgetShape shape = CheckStruct(widgets, "");
  if (failed_) {
    return std::nullopt;
  }
  return shape;
}

void WidgetChecker::CheckDevices(absl::Span<const WidgetDevice> devices) {
  std::vector<WidgetShape::Field> fields;
  absl::flat_hash_set<std::string_view> names;
  absl::flat_hash_set<const WidgetShape*> catalogs;
  for (const WidgetDevice& device : devices) {
    CheckWidgetName("", "device", device.name);
    const bool is_new = AddName("", "device", device.name, names);
    if (device.catalog == nullptr) {
      AddError("", absl::StrCat("device \"", device.name, "\" has no catalog"));
      unusable_devices_.insert(device.name);
      continue;
    }
    if (device.catalog->GetKind() != WidgetKind::kStruct) {
      AddError("", absl::StrCat("catalog of device \"", device.name,
                                "\" isn't a struct"));
      unusable_devices_.insert(device.name);
      continue;
    }
    if (is_new) {
      fields.push_back({device.name, *device.catalog});
    }
    // Devices sharing a catalog would only repeat its errors.
    if (catalogs.insert(device.catalog).second) {
      CheckCatalog(device.name, *device.catalog, "");
    }
  }
  devices_ = WidgetShape::ForStruct(std::move(fields));
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

WidgetShape WidgetChecker::CheckStruct(const WidgetStructDefinition& definition,
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
  return WidgetShape::ForStruct(std::move(fields.fields));
}

void WidgetChecker::CheckInclude(const WidgetInclude& include,
                                 const std::string& struct_path,
                                 StructFields& fields) {
  const WidgetShape* shape = FindShape(include.path, "include", struct_path);
  if (shape == nullptr) {
    return;
  }
  if (shape->GetKind() != WidgetKind::kStruct) {
    AddError(struct_path,
             absl::StrCat("include \"", include.path, "\" isn't a struct"));
    return;
  }
  for (const std::string& excluded : include.excluded) {
    if (shape->GetField(excluded) == nullptr) {
      AddError(struct_path,
               absl::StrCat("include \"", include.path, "\" excludes \"",
                            excluded, "\", which it doesn't have"));
    }
  }
  for (const WidgetShape::Field& field : shape->GetFields()) {
    // A field repeated in a catalog is the first of its name, and checking the
    // catalog reported the rest.
    if (shape->GetField(field.name) == &field.shape &&
        !absl::c_linear_search(include.excluded, field.name)) {
      AddField(struct_path, field.name, field.shape, fields);
    }
  }
}

void WidgetChecker::AddField(std::string_view struct_path,
                             std::string_view name,
                             std::optional<WidgetShape> shape,
                             StructFields& fields) {
  if (AddName(struct_path, "field", name, fields.names) && shape.has_value()) {
    fields.fields.push_back({std::string(name), std::move(*shape)});
  }
}

std::optional<WidgetShape> WidgetChecker::CheckField(
    const WidgetFieldDefinition& definition, const std::string& field_path) {
  if (const std::string* path = std::get_if<std::string>(&definition.widget)) {
    const WidgetShape* shape = FindShape(*path, "path", field_path);
    if (shape == nullptr) {
      return std::nullopt;
    }
    return *shape;
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

std::optional<WidgetShape> WidgetChecker::CheckJoin(
    const WidgetJoinDefinition& definition, const std::string& field_path) {
  if (definition.slices.empty()) {
    AddError(field_path, "join has no slices");
    return std::nullopt;
  }
  const WidgetShape* element = nullptr;
  const WidgetSlice* element_slice = nullptr;
  int64_t size = 0;
  for (const WidgetSlice& slice : definition.slices) {
    const WidgetShape* array = FindShape(slice.path, "slice", field_path);
    if (array == nullptr) {
      continue;
    }
    if (array->GetKind() != WidgetKind::kArray) {
      AddError(field_path,
               absl::StrCat(GetSliceText(slice), " isn't an array"));
      continue;
    }
    const int array_size = array->GetSize();
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
      element = array->GetElement();
      element_slice = &slice;
    } else if (*array->GetElement() != *element) {
      AddError(field_path, absl::StrCat(GetSliceText(slice),
                                        " has elements of another shape than ",
                                        GetSliceText(*element_slice)));
      continue;
    }
    size += count;
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
  return WidgetShape::ForArray(*element, static_cast<int>(size));
}

const WidgetShape* WidgetChecker::FindShape(std::string_view text,
                                            std::string_view what,
                                            std::string_view subject) {
  std::string error_message;
  std::optional<WidgetPath> path = WidgetPath::Parse(text, &error_message);
  if (!path.has_value()) {
    AddError(subject, absl::StrCat(what, " \"", text,
                                   "\" isn't a path: ", error_message));
    return nullptr;
  }
  const std::string* device_name =
      path->GetSteps().empty()
          ? nullptr
          : std::get_if<std::string>(&path->GetSteps().front());
  if (device_name == nullptr || devices_.GetField(*device_name) == nullptr) {
    if (device_name == nullptr || !unusable_devices_.contains(*device_name)) {
      AddError(subject,
               absl::StrCat(what, " \"", text, "\" doesn't start at a device"));
    }
    return nullptr;
  }
  const WidgetShape* shape = devices_.Find(*path);
  if (shape == nullptr) {
    AddError(subject, absl::StrCat(what, " \"", text, "\" names nothing"));
  }
  return shape;
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
  return WidgetChecker(errors).Check(widgets, devices);
}

}  // namespace jpr
