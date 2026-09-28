// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "jpr/scene/view_property.h"

namespace jpr {

//==============================================================================
// ViewReference
//==============================================================================

// The kind of subject a view or reference has. A reference always has a kind
// other than kNone, even while it refers to nothing.
enum class SubjectKind {
  kNone,   // No subject.
  kTrack,  // A track (see TrackReference).
  kRoute,  // A send or receive of a track (see RouteReference).
};

// A reference to a subject of some kind, or to nothing.
//
// A reference isn't a property itself, but its fields are: the properties of
// whatever subject it refers to, which follow the reference as it changes.
// Which subject it refers to is decided by its owner, according to the kind of
// reference it is.
class ViewReference {
 public:
  ViewReference(const ViewReference&) = delete;
  ViewReference& operator=(const ViewReference&) = delete;
  virtual ~ViewReference() = default;

  // The name the reference is looked up by, such as "state:master_track". It
  // is empty for a reference nothing looks up by name.
  std::string_view GetName() const { return name_; }

  // The kind of subject the reference refers to.
  SubjectKind GetKind() const { return kind_; }

  // Changes whenever what it refers to changes. Only compared for equality,
  // and unlike comparing subjects, it notices a change to a new track that
  // reuses a deleted track's memory.
  int64_t GetVersion() const { return version_; }

  // Returns the field with the name, without its namespace (so "name" for a
  // track's track:name), or null if there is no such field. The field is owned
  // by the reference, and follows it to whatever it refers to.
  virtual ViewProperty* GetField(std::string_view name) const = 0;

  // Updates what it refers to by its rules, and refreshes from REAPER what its
  // watched fields show. Its owner calls this once per run while anything may
  // show it.
  virtual void Update() = 0;

 protected:
  ViewReference(std::string_view name, SubjectKind kind)
      : name_(name), kind_(kind) {}

  // Called by the derived class whenever what it refers to changes.
  void ChangeVersion() { ++version_; }

 private:
  const std::string name_;
  const SubjectKind kind_;
  int64_t version_ = 0;
};

}  // namespace jpr
