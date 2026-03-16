// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/midi_sysex.h"

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/strings/escaping.h"

namespace jpr {

//==============================================================================
// SysexMessage
//==============================================================================

SysexMessage::SysexMessage(SysexPrefix prefix, int size)
    : prefix_(prefix), bytes_(size + prefix.GetPrefix().size() + 2, 0) {
  // The message bytes are in the format [0xF0, prefix..., data..., 0xF7].
  bytes_[0] = 0xF0;
  std::copy(prefix.GetPrefix().begin(), prefix.GetPrefix().end(),
            bytes_.begin() + 1);
  bytes_.back() = 0xF7;
}

//==============================================================================
// SysexMessageType
//==============================================================================

namespace {

absl::flat_hash_map<SysexPrefix, SysexMessageType*>& GetSysexTypeRegistry() {
  static absl::NoDestructor<absl::flat_hash_map<SysexPrefix, SysexMessageType*>>
      types;
  return *types;
}

}  // namespace

SysexMessageType* SysexMessageType::Get(const SysexPrefix& prefix) {
  auto registry = GetSysexTypeRegistry();
  if (auto it = registry.find(prefix); it != registry.end()) {
    return it->second;
  }
  return nullptr;
}

SysexMessageType::SysexMessageType(SysexPrefix prefix) : prefix_(prefix) {}

SysexMessageType::~SysexMessageType() {
  if (registered_) {
    GetSysexTypeRegistry().erase(prefix_);
  }
}

bool SysexMessageType::Register() {
  auto [it, inserted] = GetSysexTypeRegistry().emplace(prefix_, this);
  if (!inserted) {
    LOG(ERROR) << "Failed to register SysexMessageType with prefix " << prefix_
               << " because that prefix is already registered.";
    return false;
  }
  registered_ = true;
  return true;
}

}  // namespace jpr
