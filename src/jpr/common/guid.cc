// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/guid.h"

#include "sdk/reaper_plugin_functions.h"

namespace jpr {

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
