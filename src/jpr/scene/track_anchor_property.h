// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>

#include "jpr/common/anchor.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/track.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_property.h"

namespace jpr {

//==============================================================================
// TrackAnchorProperty
//==============================================================================

// A toggle that anchors a track action on its view's track while it is on (see
// TrackActions::GetAnchor()), for a mapping that turns it on while a button is
// held (a press_release mapping). Pressing the same action on another track
// then acts on the range from this one. It is added to a view, and holds the
// anchor on the view's track, unless the view has no track.
//
// The anchor is released when the property turns off, or when the view releases
// it (see View::SetAnchor()). The property always reads as off.
class TrackAnchorProperty final : public ViewProperty {
 public:
  struct Config {
    // The action whose anchor is held.
    TrackBoolProperty action = TrackBoolProperty::kSelected;

    // Modifier bits that are on while the anchor is held, if any.
    Modifiers modifier = 0;
  };

  // Creates a track anchor with the name, and adds it to the view (see
  // View::AddUserProperty()). Returns null if the name can't be added there.
  static TrackAnchorProperty* AddToView(View* view, std::string_view name,
                                        const Config& config);

  ~TrackAnchorProperty() override = default;

 protected:
  // Overrides from ViewProperty.
  void WriteBool(bool value) override;

 private:
  TrackAnchorProperty(View* view, std::string_view name, const Config& config);

  View* const view_;
  Anchor<Track>* const anchor_;
  const Modifiers modifier_;
};

}  // namespace jpr
