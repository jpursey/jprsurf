// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/guid.h"

#include <string>

#include "absl/strings/str_format.h"
#include "jpr/common/reaper_api.h"

namespace jpr {

std::string FormatGuid(const GUID& guid) {
  return absl::StrFormat("{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                         guid.Data1, guid.Data2, guid.Data3, guid.Data4[0],
                         guid.Data4[1], guid.Data4[2], guid.Data4[3],
                         guid.Data4[4], guid.Data4[5], guid.Data4[6],
                         guid.Data4[7]);
}

Guid::Guid(const GUID& guid) {
  guid_.resize(64);
  guidToString(&guid, guid_.data());
}

Guid::Guid(const GUID* guid) {
  if (guid != nullptr) {
    guid_.resize(64);
    guidToString(guid, guid_.data());
  }
}

GUID Guid::ToGUID() const {
  if (guid_.empty()) {
    return {};
  }
  GUID guid;
  stringToGuid(guid_.data(), &guid);
  return guid;
}

}  // namespace jpr
