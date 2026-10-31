// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// The documentation of the X-Touch's catalogs (see DeviceXTouch::GetCatalog()),
// apart from device_xtouch.cc, so its descriptions don't bury the catalog
// code.

#include <string_view>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/types/span.h"
#include "jpr/device/device_xtouch.h"
#include "jpr/device/widget_doc.h"

namespace jpr {

namespace {

// The modes of every light, buttons' included.
constexpr WidgetDoc::Mode kLightModes[] = {
    {"solid", "Lit when on."},
    {"blink", "Blinks when on."},
};

// The modes of a pot's ring.
constexpr WidgetDoc::Mode kRingModes[] = {
    {"dot", "One light at the value, from left to right."},
    {"center", "The lights from the center to the value."},
    {"fill", "The lights from the left to the value."},
    {"spread",
     "The lights from the center out to both sides, wider as the value "
     "rises."},
    {"dot_edges", "As dot, with the outermost lights always lit."},
    {"center_edges", "As center, with the outermost lights always lit."},
    {"fill_edges", "As fill, with the outermost lights always lit."},
    {"spread_edges", "As spread, with the outermost lights always lit."},
    {"off", "Every light off, whatever the value."},
};

// A light, or a button with one, documented with the light's modes.
struct LightDoc {
  std::string_view path;
  std::string_view description;
};

// The strips, the same on both models.
constexpr LightDoc kStripLightDocs[] = {
    {"strip.rec", "The REC button."},
    {"strip.solo", "The SOLO button."},
    {"strip.mute", "The MUTE button."},
    {"strip.select", "The SELECT button."},
};
constexpr WidgetDoc kStripDocs[] = {
    {.path = "strip", .description = "The 8 channel strips, from the left."},
    {.path = "strip.pot_button",
     .description = "Pressing the pot, which has no light."},
    {.path = "strip.fader",
     .description = "The motorized fader. Its value is its position, from 0 at "
                    "the bottom to 1 at the top, and touching it presses it."},
    {.path = "strip.pot",
     .description = "The rotary encoder above the scribble strip, with no end "
                    "stops, which turns by a delta, and its ring of 13 lights.",
     .modes = kRingModes},
    {.path = "strip.meter",
     .description = "The level meter. Its value is a linear peak level, where "
                    "1 (0 dBFS) or more lights its clip light."},
    {.path = "strip.scribble_top",
     .description = "The top line of the scribble strip, 7 characters."},
    {.path = "strip.scribble_bottom",
     .description = "The bottom line of the scribble strip, 7 characters."},
    {.path = "strip.scribble_color",
     .description = "The scribble strip's backlight, the nearest of its 7 "
                    "colors, or off for black."},
};

// The X-Touch's master section.
constexpr LightDoc kMasterLightDocs[] = {
    {"assign_track", "The TRACK button."},
    {"assign_send", "The SEND button."},
    {"assign_pan", "The PAN/SURROUND button."},
    {"assign_plugin", "The PLUG-IN button."},
    {"assign_eq", "The EQ button."},
    {"assign_inst", "The INST button."},
    {"bank_left", "The left BANK button."},
    {"bank_right", "The right BANK button."},
    {"channel_left", "The left CHANNEL button."},
    {"channel_right", "The right CHANNEL button."},
    {"flip", "The FLIP button."},
    {"global", "The GLOBAL VIEW button."},
    {"show_name_value", "The NAME/VALUE button."},
    {"show_time_beats", "The SMPTE/BEATS button."},
    {"view_midi", "The MIDI TRACKS button."},
    {"view_inputs", "The INPUTS button."},
    {"view_audio", "The AUDIO TRACKS button."},
    {"view_inst", "The AUDIO INST button."},
    {"view_aux", "The AUX button."},
    {"view_buses", "The BUSES button."},
    {"view_outputs", "The OUTPUTS button."},
    {"view_user", "The USER button."},
    {"shift", "The SHIFT button."},
    {"option", "The OPTION button."},
    {"control", "The CONTROL button."},
    {"alt", "The ALT button."},
    {"auto_read", "The READ/OFF button."},
    {"auto_write", "The WRITE button."},
    {"auto_trim", "The TRIM button."},
    {"auto_touch", "The TOUCH button."},
    {"auto_latch", "The LATCH button."},
    {"auto_group", "The GROUP button."},
    {"save", "The SAVE button."},
    {"undo", "The UNDO button."},
    {"cancel", "The CANCEL button."},
    {"enter", "The ENTER button."},
    {"marker", "The MARKER button."},
    {"nudge", "The NUDGE button."},
    {"cycle", "The CYCLE button."},
    {"drop", "The DROP button."},
    {"replace", "The REPLACE button."},
    {"click", "The CLICK button."},
    {"solo", "The SOLO button by the transport."},
    {"rewind", "The rewind button."},
    {"forward", "The fast forward button."},
    {"stop", "The stop button."},
    {"play", "The play button."},
    {"record", "The record button."},
    {"up", "The up arrow button."},
    {"down", "The down arrow button."},
    {"scrub", "The SCRUB button."},
    {"zoom", "The button in the middle of the arrows."},
    {"left", "The left arrow button."},
    {"right", "The right arrow button."},
    {"function", "The function keys, F1 to F8, from the left."},
    {"smpte_light", "The SMPTE light."},
    {"beats_light", "The BEATS light."},
    {"solo_light", "The SOLO light by the display."},
};
constexpr WidgetDoc kMasterDocs[] = {
    {.path = "master_fader",
     .description = "The master fader, the same as a strip's."},
    {.path = "timecode",
     .description = "The 10 digit timecode display, in fields of 3, 2, 2, and "
                    "3 digits."},
};

// Adds the docs of the lights, and the other docs.
void AddDocs(absl::Span<const LightDoc> lights,
             absl::Span<const WidgetDoc> others, std::vector<WidgetDoc>& docs) {
  for (const LightDoc& light : lights) {
    docs.push_back({.path = light.path,
                    .description = light.description,
                    .modes = kLightModes});
  }
  docs.insert(docs.end(), others.begin(), others.end());
}

CatalogDoc CreateCatalogDoc(DeviceXTouch::Type type) {
  CatalogDoc doc;
  if (type == DeviceXTouch::Type::kExtender) {
    doc.description =
        "The Behringer X-Touch Extender, in Mackie Control (MC) mode, whose "
        "MIDI ports show as `X-Touch-Ext`. It has 8 channel strips, the same "
        "as the X-Touch's.";
  } else {
    doc.description =
        "The Behringer X-Touch, in Mackie Control (MC) mode, whose MIDI ports "
        "show as `X-Touch`. It has 8 channel strips, and a master section "
        "with the master fader, the timecode display, and the transport and "
        "navigation buttons. Every button has a light, apart from the pots'.";
    AddDocs(kMasterLightDocs, kMasterDocs, doc.widgets);
  }
  AddDocs(kStripLightDocs, kStripDocs, doc.widgets);
  return doc;
}

}  // namespace

const CatalogDoc& DeviceXTouch::GetCatalogDoc(Type type) {
  if (type == Type::kFull) {
    static const absl::NoDestructor<CatalogDoc> s_full(
        CreateCatalogDoc(Type::kFull));
    return *s_full;
  }
  static const absl::NoDestructor<CatalogDoc> s_extender(
      CreateCatalogDoc(Type::kExtender));
  return *s_extender;
}

}  // namespace jpr
