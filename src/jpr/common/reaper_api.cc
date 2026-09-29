// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// The SDK defines the API's function pointers, and REAPERAPI_LoadAPI(), in the
// one file that defines REAPERAPI_IMPLEMENT.
#define REAPERAPI_IMPLEMENT
#include "jpr/common/reaper_api.h"

#include <string_view>

#include "absl/log/log.h"

namespace jpr {

namespace {

// Taking each function's address makes an entry without a REAPERAPI_WANT_
// line a compile error.
#define JPR_REAPER_NAME(name) ((void)&::name, #name),
const char* const kReaperFunctionNames[] = {JPR_REAPER_API(JPR_REAPER_NAME)};
#undef JPR_REAPER_NAME

// The `get_func` passed to LoadReaperApi(), while it loads.
void* (*g_get_func)(const char* name) = nullptr;

bool IsListed(std::string_view name) {
  for (const char* listed_name : kReaperFunctionNames) {
    if (listed_name == name) {
      return true;
    }
  }
  return false;
}

// Loads a function for REAPERAPI_LoadAPI(), which asks for every function with
// a REAPERAPI_WANT_ line.
void* GetListedFunction(const char* name) {
  if (!IsListed(name)) {
    LOG(ERROR) << "REAPER API function " << name
               << " has a REAPERAPI_WANT_ line, but isn't in JPR_REAPER_API.";
    return nullptr;
  }
  void* function = g_get_func(name);
  if (function == nullptr) {
    LOG(ERROR) << "REAPER API function " << name << " not found.";
  }
  return function;
}

}  // namespace

bool LoadReaperApi(void* (*get_func)(const char* name)) {
  g_get_func = get_func;
  const bool loaded = (REAPERAPI_LoadAPI(&GetListedFunction) == 0);
  g_get_func = nullptr;
  return loaded;
}

}  // namespace jpr
