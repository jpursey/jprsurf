// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

namespace jpr {

// The line of its config that a part of a spec was written on, counting from 1,
// or 0 if it wasn't read from a config, as when the spec is written in code.
// Whoever reads the config knows which file it was, and names it.
//
// Lines always compare equal, as where a part was written isn't part of what
// it says, so a spec read from a config compares equal to the same spec written
// in code.
struct SpecLine {
  friend bool operator==(SpecLine, SpecLine) { return true; }

  int number = 0;
};

// Returns an error about a part of a spec: its line (if it has one), then what
// it is about (if not empty), then the message, such as `line 12: xtouch: type
// "XTouh" isn't a device type`.
std::string GetSpecError(SpecLine line, std::string_view subject,
                         std::string_view message);

}  // namespace jpr
