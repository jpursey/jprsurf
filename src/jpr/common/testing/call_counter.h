// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/call_cost.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {

//==============================================================================
// CallCounts
//
// The calls to the REAPER API a CallCounter counted, by cost (see CallCost).
//==============================================================================

struct CallCounts {
  // Compares everything but `calls`.
  bool operator==(const CallCounts& other) const;

  std::string ToString() const;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const CallCounts& counts) {
    sink.Append(counts.ToString());
  }

  int cheap = 0;
  int moderate = 0;
  int refreshes = 0;
  int undo_points = 0;

  // The actions Main_OnCommand() ran, in order.
  std::vector<int> commands;

  // How many times each function was called, by name, to show what changed
  // when two counts differ (ToString() lists them by name). It isn't compared.
  absl::flat_hash_map<std::string_view, int> calls;
};

//==============================================================================
// CallCounter
//
// Counts the calls to the REAPER API by cost (see GetCallCost()), for as long
// as it exists, and fails the test, naming the calls, for an entry point that:
// - Refreshes REAPER's UI more than once.
// - Adds more than one undo point.
// - Repeats a refresh or a command: makes it twice with the same repeat key
//   (see CallCost::repeat_key_args), such as a track's mute, or a route's
//   volume.
//
// Entry points are the fake's (see "Checks" in FakeReaper): each call on the
// surface, such as a run, including what the surface does in the calls REAPER
// makes back from inside its functions, and the test's own calls between them.
// Each is checked as it ends, and calls not yet ended when the counter is
// destroyed are checked then. A gesture's work is done in a run, so this
// checks every gesture, however a test makes it.
//
// A refresh a PreventUIRefresh() scope puts off (CallCost::batched) is
// moderate inside a scope, and the call that ends the outermost scope is the
// refresh. Any other refresh is one inside a scope too, so in a scope that
// ends, it is a second. Undo points are those the extension adds: REAPER's
// own, such as for a track volume moved from a surface, aren't calls.
//
// It hooks every function on the API list over whatever is loaded, so it is
// created after anything that makes REAPER's calls back to a surface (a
// SurfaceNotifier), and then also counts the calls the surface makes from
// inside them. Hooks installed over its own must be removed before it is
// destroyed (see gb::FunctionHook). Only one may exist at a time.
//==============================================================================

class CallCounter final {
 public:
  // Starts counting the calls made with `reaper` loaded as the REAPER API.
  // `reaper` must outlive this.
  explicit CallCounter(FakeReaper* reaper);
  CallCounter(const CallCounter&) = delete;
  CallCounter& operator=(const CallCounter&) = delete;

  // Checks the calls since the last entry point ended.
  ~CallCounter();

  // Returns the calls counted since the last call, or since this was created,
  // and starts again. A test compares them with EXPECT_EQ, and a failure
  // prints each function's count.
  CallCounts TakeCounts() { return std::exchange(counts_, {}); }

 private:
  // A function, and the leading arguments of a call to it that make up its
  // repeat key, with any after them zero.
  using RepeatKey = std::tuple<std::string_view, int64_t, int64_t>;

  // What the current entry point has done that the rules limit.
  struct EntryPointCalls {
    std::vector<std::string_view> refreshes;
    int undo_points = 0;
    absl::flat_hash_set<RepeatKey> keys;
    std::vector<std::string_view> repeats;
  };

  // A gb::FunctionHook's Hook, which counts each call to the function `name`
  // once it returns.
  class Hook final {
   public:
    explicit Hook(std::string_view name);
    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;
    ~Hook() = default;

    template <typename Function, typename... Args>
    decltype(auto) Call(Function original, Args... args) {
      // The leading arguments that make up the call's repeat key, as numbers.
      int64_t key_args[kMaxRepeatKeyArgs] = {};
      int index = 0;
      ((index < cost_.repeat_key_args
            ? (void)(key_args[index++] = ToKeyArg(args))
            : (void)0),
       ...);
      const bool in_scope = s_counter_->reaper_->IsUIRefreshPrevented();
      const absl::Cleanup count = [&] {
        s_counter_->Count(*this, key_args, in_scope);
      };
      return original(args...);
    }

   private:
    friend class CallCounter;

    // Returns `arg` as a number, to compare: a pointer by its address, and any
    // other by its value.
    template <typename T>
    static int64_t ToKeyArg(T arg) {
      if constexpr (std::is_pointer_v<T>) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(arg));
      } else {
        return static_cast<int64_t>(arg);
      }
    }

    const std::string_view name_;
    const CallCost cost_;
  };

  // The most leading arguments a repeat key has (see
  // CallCost::repeat_key_args).
  static constexpr int kMaxRepeatKeyArgs = 2;

  // Counts a call to `hook`'s function, with the arguments of its repeat key,
  // made with a PreventUIRefresh() scope open if `in_scope`. A command's
  // repeat key is its action.
  void Count(const Hook& hook, const int64_t* key_args, bool in_scope);

  // Counts a call to `hook`'s function as a refresh.
  void AddRefresh(const Hook& hook);

  // Adds a refresh or a command to the entry point's calls, failing if it
  // repeats.
  void AddRepeatKey(const Hook& hook, const int64_t* key_args);

  // Checks the current entry point against the rules (see the class comment),
  // and starts the next.
  void CheckEntryPoint();

  // The counter that exists, if any, which the hooks count with.
  static CallCounter* s_counter_;

  FakeReaper* const reaper_;
  CallCounts counts_;
  EntryPointCalls entry_point_calls_;

  // Last, so the hooks are removed first.
  ReaperApiHooks<Hook> hooks_;
};

}  // namespace jpr
