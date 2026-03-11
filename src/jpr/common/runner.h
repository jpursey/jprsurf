// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"

namespace jpr {

class RunRegistry;

// RunTime holds the current time that Run() was called, in both a precise and
// coarse form, as needed by different Reaper APIs.
struct RunTime {
  double precise;       // Time in seconds, with high precision.
  unsigned int coarse;  // Time in milliseconds, with lower precision.
};

// This class represents a handle to a runnable.
// callback can be set on the handle, which will be called when the Runner's
// Run() method is called. The handle is automatically unregistered from the
// Runner when it is destroyed.
class RunHandle final {
 public:
  RunHandle() = default;
  RunHandle(const RunHandle&) = delete;
  RunHandle& operator=(const RunHandle&) = delete;
  RunHandle(RunHandle&& other);
  RunHandle& operator=(RunHandle&& other);
  ~RunHandle();

  bool IsRegistered() const { return registry_ != nullptr; }

 private:
  friend class RunRegistry;
  explicit RunHandle(RunRegistry* registry, int id)
      : registry_(registry), id_(id) {}

  RunRegistry* registry_ = nullptr;
  int id_ = 0;
};

// RunRegistry holds a set of runnables that can be run by the Runner derived
// class.
class RunRegistry {
 public:
  // A Runnable is a function that will be called when Run() is called on the
  // Runner. It takes the current time as an input.
  using Runnable = absl::AnyInvocable<void(const RunTime& time)>;

  RunRegistry(const RunRegistry&) = delete;
  RunRegistry& operator=(const RunRegistry&) = delete;

  // Adds the handle to the runner and returns it. A RunHandle can be used to
  // register a runnable that will be called when Run() is called on this
  // runner. The runnable will be automatically unregistered when the handle is
  // destroyed.
  RunHandle AddRunnable(Runnable runnable);

 protected:
  RunRegistry() = default;
  ~RunRegistry() = default;

  // Runs all registered runnables. Should be called from Runner::Run().
  void DoRun();

 private:
  friend class RunHandle;

  absl::flat_hash_set<int> cleared_runnables_;
  absl::flat_hash_map<int, Runnable> runnables_;
  int next_id_ = 1;
};

// This class manages a set of runnables that can be registered to run when
// Run() is called. Runnables are registered by calling AddRunnable().
class Runner final : public RunRegistry {
 public:
  Runner() = default;
  Runner(const Runner&) = delete;
  Runner& operator=(const Runner&) = delete;
  ~Runner() = default;

  // Runs all set runnables in added handles. Should be called from
  // ControlSurface::Run().
  void Run() { DoRun(); }

 private:
  friend class RunHandle;
};

}  // namespace jpr
