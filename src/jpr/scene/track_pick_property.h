// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>

#include "jpr/scene/track_reference.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_property.h"

namespace jpr {

//==============================================================================
// TrackPickProperty
//==============================================================================

// An action that picks a track: it sets a track reference to a source's track,
// if the source has a track. Whether a pick should happen at all is up to the
// condition of the mapping that triggers it, and anything else the same press
// does (such as entering a mode) is another mapping on it.
//
// With no source, it picks its view's track, so it must be added to a view
// with a track subject (see View::AddUserProperty()). Otherwise it does
// nothing.
class TrackPickProperty final : public ViewProperty {
 public:
  // The pick sets `reference` to the track of `source`, or of its view if
  // `source` is null. Both must outlive the pick.
  TrackPickProperty(std::string_view name, TrackReference& reference,
                    const TrackReference* source = nullptr)
      : ViewProperty(name, Type::kAction),
        reference_(reference),
        source_(source) {}
  ~TrackPickProperty() override = default;

 protected:
  // Overrides from ViewProperty.
  void TriggerAction() override;
  void SetView(View* view) override { view_ = view; }

 private:
  TrackReference& reference_;
  const TrackReference* const source_;
  View* view_ = nullptr;  // The view that owns this, if any.
};

}  // namespace jpr
