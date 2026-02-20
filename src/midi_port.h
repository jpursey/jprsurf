// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <vector>

#include "reaper_plugin.h"
#include "runner.h"

namespace jpr {

class MidiIn {
 public:
  static std::vector<std::unique_ptr<MidiIn>> GetPorts();

  MidiIn(const MidiIn&) = delete;
  MidiIn& operator=(const MidiIn&) = delete;
  ~MidiIn();

  int GetIndex() const { return index_; }
  std::string_view GetName() const { return name_; }

  bool Open(RunRegistry& registry);
  void Close();

 private:
  MidiIn(int index, std::string name) : index_(index), name_(std::move(name)) {}

  void Poll(const RunTime& time);

  int index_ = -1;
  std::string name_;
  midi_Input* port_ = nullptr;
  RunHandle run_handle_;
};

}  // namespace jpr