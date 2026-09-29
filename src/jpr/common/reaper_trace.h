// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <filesystem>
#include <memory>

#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// ReaperTrace
//
// Writes every call between JPRSurf and REAPER to a file, in order, for as long
// as it exists: every function on the REAPER API list (see reaper_api.h), and
// every call REAPER makes on a control surface registered through
// TraceSurfaceRegistration().
//
// Each call is one line, with its arguments, its result, and the values of its
// output parameters after the call (shown as "out" in the arguments). Tracks
// are written by their number and name. Calls made during another call, such
// as a callback REAPER makes on the surface from inside a setter, are indented
// beneath it. Each top level call starts with the seconds since the trace
// started.
//
// Most calls only read state, such as REAPER polling the surface with Run(),
// and the surface reading REAPER's state in it. A top level call where every
// call only reads is left out, and the next call that is written is preceded
// by how many were. So the steady state is left out, and each event is written
// with the whole call it happened in.
//
// A trace is for finding out what REAPER does, and is far too slow to leave on.
// While no ReaperTrace exists, nothing is hooked or wrapped, so it costs
// nothing.
//==============================================================================

class ReaperTrace final {
 public:
  // Starts tracing to the file at `path`, replacing it. The REAPER API must
  // already be loaded, and only one ReaperTrace may exist at a time.
  explicit ReaperTrace(const std::filesystem::path& path);
  ReaperTrace(const ReaperTrace&) = delete;
  ReaperTrace& operator=(const ReaperTrace&) = delete;
  ~ReaperTrace();

  // Returns the registration to give REAPER for the control surface type
  // `reg`: while a trace exists, one whose surfaces are traced, and otherwise
  // `reg` itself. Only one control surface type can be traced.
  //
  // `reg` must remain valid for as long as the extension is loaded. Surfaces
  // created while no trace exists aren't traced, and surfaces that outlive the
  // trace stop being traced.
  static reaper_csurf_reg_t* TraceSurfaceRegistration(reaper_csurf_reg_t* reg);

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace jpr
