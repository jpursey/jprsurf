// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/catalog.h"

#include "jpr/scene/route_properties.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/state_properties.h"
#include "jpr/scene/track_properties.h"
#include "jpr/scene/view_list.h"

namespace jpr {

std::string_view GetPropertyAccessName(PropertyAccess access) {
  switch (access) {
    case PropertyAccess::kRead:
      return "read";
    case PropertyAccess::kWrite:
      return "write";
    case PropertyAccess::kTrigger:
      return "trigger";
  }
  return "unknown";
}

absl::Span<const PropertyNamespaceInfo> GetPropertyCatalog() {
  static const PropertyNamespaceInfo kCatalog[] = {
      {kCmdNamespace,
       "REAPER's commands, by command id: a number, such as \"cmd:40029\", or "
       "a named command id registered by an extension or script, such as "
       "\"cmd:_SWS_ABOUT\". A command with a toggle state is a toggle, which "
       "writing runs if the value changes, and any other command is an action. "
       "A command REAPER doesn't have is no property."},
      {kStateNamespace,
       "REAPER's global state, and the scene's built in references, whose "
       "fields are their subjects' properties, such as "
       "\"state:master_track.volume\".",
       GetStatePropertyInfo(), Scene::GetBuiltInReferenceInfo()},
      {kModNamespace,
       "The surface's modifiers, which modifier sets require. A config may "
       "declare more.",
       Scene::GetBuiltInModifierInfo()},
      {kTrackNamespace,
       "The properties of the track a view shows, and the fields of a track "
       "reference, such as \"state:master_track.volume\" for \"track:volume\".",
       GetTrackPropertyInfo()},
      {kRouteNamespace,
       "The properties of the send or receive a view shows, and the track at "
       "its other end, whose fields are track properties, such as "
       "\"route:other_track.name\".",
       GetRoutePropertyInfo(), GetRouteReferenceInfo()},
      {kViewNamespace,
       "The properties of the view a mapping is in, which act on the view's "
       "list, or its parent's. Each is on only the views it needs.",
       kViewProperties},
  };
  return kCatalog;
}

absl::Span<const ComponentKindInfo> GetComponentKinds() {
  static constexpr ParamInfo kEnumeratedValueParams[] = {
      {"values", ParamKind::kNewNameList, ParamUse::kRequired,
       "Its values, in order."},
      {"initial", ParamKind::kValueName, ParamUse::kOptional,
       "The value it starts as. Without one, it starts as the first."},
  };
  static constexpr ParamInfo kTrackReferenceParams[] = {
      {"fallback", ParamKind::kTrackReference, ParamUse::kOptional,
       "The reference whose track it starts as, and returns to whenever it "
       "refers to nothing, or its track is deleted. Without one, it starts "
       "as, and returns to, nothing."},
      {"follow", ParamKind::kTrackReference, ParamUse::kOptional,
       "The reference it follows: whenever that reference's track changes to "
       "one on the surface, this one changes to it too."},
  };

  // The track actions, by the names of the track: properties they act on, in
  // the order of kTrackBoolProperties.
  static constexpr std::string_view kTrackActions[] = {
      TrackProperties::kSelected.substr(kTrackNamespace.size()),
      TrackProperties::kMute.substr(kTrackNamespace.size()),
      TrackProperties::kSolo.substr(kTrackNamespace.size()),
      TrackProperties::kRecArm.substr(kTrackNamespace.size()),
  };
  static constexpr ParamInfo kTrackAnchorParams[] = {
      {"action", ParamKind::kChoice, ParamUse::kRequired,
       "The track action whose anchor it holds.", kTrackActions},
      {"modifier", ParamKind::kModifier, ParamUse::kOptional,
       "A modifier that is on while it is held."},
  };
  static constexpr ParamInfo kTrackPickParams[] = {
      {"reference", ParamKind::kTrackReference, ParamUse::kRequired,
       "The reference it sets, which must be one the config declares."},
      {"source", ParamKind::kTrackReference, ParamUse::kTopLevelOnly,
       "The reference whose track it picks. On a view, it picks the view's "
       "track."},
  };
  static constexpr ComponentKindInfo kComponentKinds[] = {
      {"modifier", true, false,
       "A \"mod:\" toggle backed by a modifier bit, which modifier sets can "
       "require. A scene has up to 60 besides the built in ones."},
      {"enumerated_value", true, false,
       "A \"user:\" enumerated property, which is always exactly one of its "
       "values, and whose text is its value's name. Mappings set it, and "
       "conditions on its values enable views and mappings.",
       kEnumeratedValueParams},
      {"track_reference", true, false,
       "A writable \"user:\" reference to a track, or to nothing, which views "
       "can be bound to, and which navigation and picks set.",
       kTrackReferenceParams},
      {"track_anchor", false, true,
       "A \"user:\" toggle, for a held mapping. While it is on, it holds its "
       "action's anchor on the view's track, so the action on another track "
       "acts on the range between them. The anchor is released with the "
       "button, or when the view's subject changes.",
       kTrackAnchorParams},
      {"track_pick", true, true,
       "A \"user:\" action that sets a track reference to the source's track, "
       "if it has one.",
       kTrackPickParams},
  };
  return kComponentKinds;
}

}  // namespace jpr
