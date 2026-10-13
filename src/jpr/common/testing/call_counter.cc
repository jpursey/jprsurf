// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/call_counter.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gtest/gtest.h"

namespace jpr {

//==============================================================================
// CallCounts
//==============================================================================

bool CallCounts::operator==(const CallCounts& other) const {
  return cheap == other.cheap && moderate == other.moderate &&
         refreshes == other.refreshes && undo_points == other.undo_points &&
         commands == other.commands;
}

std::string CallCounts::ToString() const {
  std::string text =
      absl::StrCat("{cheap: ", cheap, ", moderate: ", moderate,
                   ", refreshes: ", refreshes, ", undo_points: ", undo_points,
                   ", commands: [", absl::StrJoin(commands, ", "), "]}");
  std::vector<std::pair<std::string_view, int>> sorted(calls.begin(),
                                                       calls.end());
  absl::c_sort(sorted);
  for (const auto& [function, count] : sorted) {
    absl::StrAppend(&text, "\n  ", function, ": ", count);
  }
  return text;
}

//==============================================================================
// CallCounter
//==============================================================================

CallCounter* CallCounter::s_counter_ = nullptr;

CallCounter::CallCounter(FakeReaper* reaper) : reaper_(reaper) {
  CHECK(s_counter_ == nullptr) << "Only one CallCounter may exist at a time";
  s_counter_ = this;
  reaper_->SetEntryPointCheck([this] { CheckEntryPoint(); });
}

CallCounter::~CallCounter() {
  reaper_->SetEntryPointCheck({});
  CheckEntryPoint();
  s_counter_ = nullptr;
}

void CallCounter::Count(const Hook& hook, const int64_t* key_args,
                        bool deferred) {
  ++counts_.calls[hook.name_];

  // The call that ends the outermost deferred refresh pays for what it put
  // off, if anything.
  if (deferred && !reaper_->IsUIRefreshPrevented() &&
      std::exchange(entry_point_calls_.refresh_deferred, false)) {
    AddRefresh(hook);
    return;
  }

  const CallCost& cost = hook.cost_;
  switch (cost.kind) {
    case CallCost::Kind::kCheap:
      ++counts_.cheap;
      break;
    case CallCost::Kind::kModerate:
      ++counts_.moderate;
      break;
    case CallCost::Kind::kRefresh:
      if (cost.deferrable && deferred) {
        ++counts_.moderate;
        entry_point_calls_.refresh_deferred = true;
      } else {
        AddRefresh(hook);
      }
      AddRepeatKey(hook, key_args);
      break;
    case CallCost::Kind::kUndoPoint:
      ++counts_.undo_points;
      ++entry_point_calls_.undo_points;
      break;
    case CallCost::Kind::kCommand:
      counts_.commands.push_back(static_cast<int>(key_args[0]));
      AddRepeatKey(hook, key_args);
      break;
  }
}

void CallCounter::AddRefresh(const Hook& hook) {
  ++counts_.refreshes;
  entry_point_calls_.refreshes.push_back(hook.name_);
}

void CallCounter::AddRepeatKey(const Hook& hook, const int64_t* key_args) {
  const RepeatKey key(hook.name_, key_args[0], key_args[1]);
  if (!entry_point_calls_.keys.insert(key).second) {
    entry_point_calls_.repeats.push_back(hook.name_);
  }
}

void CallCounter::CheckEntryPoint() {
  const EntryPointCalls& calls = entry_point_calls_;
  if (calls.refreshes.size() > 1) {
    ADD_FAILURE() << "An entry point refreshed REAPER's UI "
                  << calls.refreshes.size() << " times, with "
                  << absl::StrJoin(calls.refreshes, ", ")
                  << ". A gesture pays one refresh at most: change several "
                     "tracks in one deferred refresh, as a run is.";
  }
  if (calls.undo_points > 1) {
    ADD_FAILURE() << "An entry point added " << calls.undo_points
                  << " undo points. A gesture adds one at most.";
  }
  if (!calls.repeats.empty()) {
    ADD_FAILURE() << "An entry point repeated "
                  << absl::StrJoin(calls.repeats, ", ")
                  << " on the same track, route, or action. Each call after "
                     "the first pays for a change that is then replaced.";
  }
  entry_point_calls_ = {};
}

//------------------------------------------------------------------------------
// Hook
//------------------------------------------------------------------------------

CallCounter::Hook::Hook(std::string_view name)
    : name_(name), cost_(GetCallCost(name)) {
  CHECK(cost_.repeat_key_args <= kMaxRepeatKeyArgs)
      << name << "'s repeat key has more arguments than a CallCounter keeps";

  // A command is counted by its action, the first argument of its key.
  CHECK(cost_.kind != CallCost::Kind::kCommand || cost_.repeat_key_args > 0)
      << name << "'s repeat key must have its action";
}

}  // namespace jpr
