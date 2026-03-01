// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "absl/hash/hash.h"
#include "absl/strings/str_format.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

// This is a wrapper around GUID that provides hashing and string formatting
// support.
//
// This can be used as a key in hash maps and other data structures. It also
// provides a conversion to and from GUID, and can be used to represent an
// uninitialized or invalid GUID with an empty string.
//
// This is an immutable class.
class Guid {
 public:
  // Creates an empty GUID.
  Guid() = default;

  // Creates a GUID from the provided GUID struct.
  explicit Guid(const GUID& guid);

  // Creates GUID from the provided GUID struct pointer. If the pointer is null,
  // this will create an empty GUID.
  explicit Guid(const GUID* guid);

  Guid(const Guid&) = default;
  Guid& operator=(const Guid&) = default;
  Guid(Guid&&) = default;
  Guid& operator=(Guid&&) = default;
  ~Guid() = default;

  // Operators for comparison and hashing.
  bool operator==(const Guid& other) const { return guid_ == other.guid_; }
  auto operator<=>(const Guid& other) const { return guid_ <=> other.guid_; }

  // Returns true if this is an empty GUID, which represents a
  // default-constructed or null GUID.
  bool IsEmpty() const { return guid_.empty(); }

  // Converts this Guid to a GUID.
  //
  // This will return a zero value GUID, if the internal GUID is empty.
  GUID ToGUID() const;

  // Hash function the GUID.
  template <typename H>
  friend H AbslHashValue(H h, const Guid& guid) {
    return H::combine(std::move(h), guid.guid_);
  }

  // String formatting support for the GUID.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Guid& guid) {
    absl::Format(&sink, "%s", guid.guid_);
  }

 private:
  std::string guid_;
};

}  // namespace jpr
