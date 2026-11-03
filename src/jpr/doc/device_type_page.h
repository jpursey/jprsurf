// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "absl/types/span.h"
#include "jpr/device/device_types.h"

namespace jpr {

// Returns the type's page, in markdown: its description, then a table of its
// catalog's widgets in order, with each one's path, inputs, outputs, mode
// names, and description, then a table of each set of modes that controls
// share. An array is listed once, as `strip[0..7]`, with its element's
// widgets after it, as `strip[i].fader`.
//
// The type's catalog doc should be valid (see ValidateCatalogDoc()): a widget
// or mode it doesn't document has no description.
std::string GenerateDeviceTypePage(const DeviceType& type);

// Returns the name of the type's page file, such as "XTouch.md". The pages,
// and the index page, are all the .md files in docs/user/devices.
std::string GetDeviceTypePageName(const DeviceType& type);

// The name of the device types' index page.
inline constexpr std::string_view kDeviceTypesIndexPageName = "README.md";

// Returns the index page of the device types, in markdown: a table of them,
// each linking to its page, with its catalog's description.
std::string GenerateDeviceTypesIndexPage(absl::Span<const DeviceType> types);

}  // namespace jpr
