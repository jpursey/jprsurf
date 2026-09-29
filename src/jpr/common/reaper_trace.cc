// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/reaper_trace.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "jpr/common/reaper_api.h"

namespace jpr {

namespace {

// Whether a parameter of type T is an output: a pointer to a number, text, or a
// GUID that the function writes to.
template <typename T>
constexpr bool kIsOutput = false;
template <typename T>
constexpr bool kIsOutput<T*> =
    !std::is_const_v<T> && (std::is_arithmetic_v<T> || std::is_same_v<T, GUID>);

// Calls that only read state: REAPER functions that return what REAPER already
// knows, and callbacks where REAPER polls the surface. A top level call is only
// written if something in it isn't quiet, so the steady state polling of each
// run is left out. Anything not listed counts as an event, so a function newly
// added to the API list is always written until it is listed here.
//
// Each entry takes the address of what it names, so a name that isn't on the
// API list, or isn't a surface method, doesn't compile.
#define JPR_QUIET_CALL(name) ((void)&::name, std::string_view(#name))
#define JPR_QUIET_SURFACE_CALL(method) \
  ((void)&IReaperControlSurface::method, std::string_view("csurf/" #method))
constexpr std::string_view kQuietCalls[] = {
    JPR_QUIET_CALL(AnyTrackSolo),
    JPR_QUIET_CALL(CountSelectedMediaItems),
    JPR_QUIET_CALL(CountSelectedTracks),
    JPR_QUIET_CALL(CountSelectedTracks2),
    JPR_QUIET_CALL(CountTracks),
    JPR_QUIET_CALL(format_timestr_pos),
    JPR_QUIET_CALL(GetCursorPosition),
    JPR_QUIET_CALL(GetGlobalAutomationOverride),
    JPR_QUIET_CALL(GetMasterTrack),
    JPR_QUIET_CALL(GetMediaTrackInfo_Value),
    JPR_QUIET_CALL(GetMIDIInputName),
    JPR_QUIET_CALL(GetMIDIOutputName),
    JPR_QUIET_CALL(GetNumMIDIInputs),
    JPR_QUIET_CALL(GetNumMIDIOutputs),
    JPR_QUIET_CALL(GetParentTrack),
    JPR_QUIET_CALL(GetPlayPosition),
    JPR_QUIET_CALL(GetPlayState),
    JPR_QUIET_CALL(GetSelectedTrack),
    JPR_QUIET_CALL(GetSelectedTrack2),
    JPR_QUIET_CALL(GetToggleCommandState),
    JPR_QUIET_CALL(GetTrack),
    JPR_QUIET_CALL(GetTrackColor),
    JPR_QUIET_CALL(GetTrackGUID),
    JPR_QUIET_CALL(GetTrackNumSends),
    JPR_QUIET_CALL(GetTrackReceiveUIMute),
    JPR_QUIET_CALL(GetTrackReceiveUIVolPan),
    JPR_QUIET_CALL(GetTrackSendUIMute),
    JPR_QUIET_CALL(GetTrackSendUIVolPan),
    JPR_QUIET_CALL(GetTrackState),
    JPR_QUIET_CALL(GetTrackUIVolPan),
    JPR_QUIET_CALL(guidToString),
    JPR_QUIET_CALL(IsProjectDirty),
    JPR_QUIET_CALL(kbd_getTextFromCmd),
    JPR_QUIET_CALL(mkpanstr),
    JPR_QUIET_CALL(mkvolstr),
    JPR_QUIET_CALL(NamedCommandLookup),
    JPR_QUIET_CALL(stringToGuid),
    JPR_QUIET_CALL(time_precise),
    JPR_QUIET_CALL(Track_GetPeakInfo),
    JPR_QUIET_CALL(Undo_CanRedo2),
    JPR_QUIET_SURFACE_CALL(GetTouchState),
    JPR_QUIET_SURFACE_CALL(IsKeyDown),
    JPR_QUIET_SURFACE_CALL(Run),
};
#undef JPR_QUIET_CALL
#undef JPR_QUIET_SURFACE_CALL

bool IsQuiet(std::string_view name) {
  return absl::c_linear_search(kQuietCalls, name);
}

// What the parameters of an Extended() call point to, from the comments on the
// CSURF_EXT_ constants in reaper_plugin.h.
enum class ExtendedParam {
  kPointer,  // Unused, or not known.
  kTrack,    // MediaTrack*
  kInt,      // int*
  kDouble,   // double*
  kFlag,     // Null for false, anything else for true.
  kValue,    // An integer in the pointer itself.
};

// A known Extended() call. Parameters that aren't listed are kPointer.
struct ExtendedCall {
  int call;
  const char* name;
  ExtendedParam params[3];
};

using enum ExtendedParam;
#define JPR_EXTENDED_CALL(call, ...) \
  {                                  \
    call, #call, { __VA_ARGS__ }     \
  }
constexpr ExtendedCall kExtendedCalls[] = {
    JPR_EXTENDED_CALL(CSURF_EXT_RESET),
    JPR_EXTENDED_CALL(CSURF_EXT_SETINPUTMONITOR, kTrack, kInt),
    JPR_EXTENDED_CALL(CSURF_EXT_SETMETRONOME, kFlag),
    JPR_EXTENDED_CALL(CSURF_EXT_SETAUTORECARM, kFlag),
    JPR_EXTENDED_CALL(CSURF_EXT_SETRECMODE, kInt),
    JPR_EXTENDED_CALL(CSURF_EXT_SETSENDVOLUME, kTrack, kInt, kDouble),
    JPR_EXTENDED_CALL(CSURF_EXT_SETSENDPAN, kTrack, kInt, kDouble),
    JPR_EXTENDED_CALL(CSURF_EXT_SETFXENABLED, kTrack, kInt, kFlag),
    JPR_EXTENDED_CALL(CSURF_EXT_SETFXPARAM, kTrack, kInt, kDouble),
    JPR_EXTENDED_CALL(CSURF_EXT_SETFXPARAM_RECFX, kTrack, kInt, kDouble),
    JPR_EXTENDED_CALL(CSURF_EXT_SETBPMANDPLAYRATE, kDouble, kDouble),
    JPR_EXTENDED_CALL(CSURF_EXT_SETLASTTOUCHEDFX, kTrack, kInt, kInt),
    JPR_EXTENDED_CALL(CSURF_EXT_SETFOCUSEDFX, kTrack, kInt, kInt),
    JPR_EXTENDED_CALL(CSURF_EXT_SETLASTTOUCHEDTRACK, kTrack),
    JPR_EXTENDED_CALL(CSURF_EXT_SETMIXERSCROLL, kTrack),
    JPR_EXTENDED_CALL(CSURF_EXT_SETPAN_EX, kTrack, kDouble, kInt),
    JPR_EXTENDED_CALL(CSURF_EXT_SETRECVVOLUME, kTrack, kInt, kDouble),
    JPR_EXTENDED_CALL(CSURF_EXT_SETRECVPAN, kTrack, kInt, kDouble),
    JPR_EXTENDED_CALL(CSURF_EXT_SETFXOPEN, kTrack, kInt, kFlag),
    JPR_EXTENDED_CALL(CSURF_EXT_SETFXCHANGE, kTrack, kValue),
    JPR_EXTENDED_CALL(CSURF_EXT_SETPROJECTMARKERCHANGE),
    JPR_EXTENDED_CALL(CSURF_EXT_TRACKFX_PRESET_CHANGED, kTrack, kInt),
    JPR_EXTENDED_CALL(CSURF_EXT_SUPPORTS_EXTENDED_TOUCH),
    JPR_EXTENDED_CALL(CSURF_EXT_MIDI_DEVICE_REMAP, kInt, kInt, kInt),
};
#undef JPR_EXTENDED_CALL

// The width of the time at the start of each top level line, including the
// space after it.
constexpr int kTimeWidth = 11;

// How often the file is flushed, so a crash loses little of the trace. Some
// callbacks, such as IsKeyDown(), are called constantly, so flushing after
// every call would be a write for each one.
constexpr absl::Duration kFlushInterval = absl::Milliseconds(100);

//==============================================================================
// Tracer
//
// Formats calls, and writes them to the trace file.
//==============================================================================

class Tracer final {
 public:
  explicit Tracer(const std::filesystem::path& path);
  Tracer(const Tracer&) = delete;
  Tracer& operator=(const Tracer&) = delete;
  ~Tracer();

  // Traces a call to `name`, which calls `function(args...)` and returns its
  // result. `quiet` is whether it only reads state (see kQuietCalls). Calls
  // made while the trace is formatting a call are passed straight through.
  template <typename Function, typename... Args>
  auto Call(std::string_view name, bool quiet, Function function, Args... args);

  // Traces a call to the Extended() callback of `surface`.
  int CallExtended(IReaperControlSurface& surface, int call, void* param1,
                   void* param2, void* param3);

 private:
  // Starts and ends a call to `name`. `call` is its name and arguments, and
  // `result` and `outputs` are its formatted result and outputs, if any. A
  // call's line is added once it ends, or when another call starts inside it.
  void BeginCall(bool quiet, std::string call);
  void EndCall(std::string_view name, std::string_view result,
               std::string_view outputs);

  // Adds a line to the current top level call.
  void AddLine(int depth, std::string_view text);

  // Writes the current top level call to the file if anything in it wasn't
  // quiet, and otherwise counts it as quiet.
  void EndTopLevelCall();

  // Writes how many quiet top level calls were left out since the last one
  // that was written, if any.
  void WriteQuietCount();

  template <typename... Args>
  std::string FormatCall(std::string_view name, Args... args);
  template <typename... Args>
  std::string FormatOutputs(Args... args);
  template <typename Result, typename... Args>
  std::string FormatResult(Result result, Args... args);
  std::string FormatResult(void* result, MediaTrack* track, int category,
                           int send_index, const char* parmname,
                           void* set_new_value);
  std::string FormatExtended(int call, void* param1, void* param2,
                             void* param3);
  std::string FormatExtendedParam(ExtendedParam param, void* value);

  template <typename Arg>
  std::string FormatArg(Arg arg);
  template <typename Arg>
  void AppendOutput(std::vector<std::string>& outputs, Arg arg);
  std::string FormatOutput(char* value);
  template <typename T>
  std::string FormatOutput(T* value);

  std::string FormatValue(bool value);
  std::string FormatValue(int value);
  std::string FormatValue(double value);
  std::string FormatValue(const char* value);
  std::string FormatValue(const GUID& value);
  std::string FormatValue(const GUID* value);
  std::string FormatValue(MediaTrack* value);
  std::string NameTrack(MediaTrack* value);
  std::string FormatValue(const void* value);

  std::ofstream file_;
  const absl::Time start_time_;
  absl::Time call_time_;    // When the current top level call started.
  absl::Time flush_time_;   // When the file was last flushed.
  int depth_ = 0;           // How many calls are in progress.
  std::string pending_;     // The innermost call, until it is added.
  std::string lines_;       // The current top level call's lines.
  bool has_event_ = false;  // Whether anything in it wasn't quiet.
  int quiet_count_ = 0;     // Top level calls left out since the last written.

  // Set while formatting calls REAPER (to name a track), so those calls aren't
  // traced.
  bool formatting_ = false;

  // How each track is written, so REAPER is asked once rather than each time.
  // A call that isn't quiet may rename, move, or delete a track, and so may
  // REAPER between top level calls, so this is cleared when such a call starts
  // and returns, and after each top level call.
  absl::flat_hash_map<MediaTrack*, std::string> track_names_;
};

Tracer::Tracer(const std::filesystem::path& path)
    : file_(path, std::ios::trunc), start_time_(absl::Now()) {
  if (!file_.is_open()) {
    LOG(ERROR) << "Failed to open REAPER trace file " << path.string();
    return;
  }
  file_ << "JPRSurf REAPER trace, started "
        << absl::FormatTime("%Y-%m-%d %H:%M:%S", start_time_,
                            absl::LocalTimeZone())
        << "\n";
  file_.flush();
  LOG(INFO) << "Tracing REAPER calls to " << path.string();
}

Tracer::~Tracer() {
  WriteQuietCount();
  file_ << "Trace ended\n";
  LOG(INFO) << "REAPER trace ended";
}

template <typename Function, typename... Args>
auto Tracer::Call(std::string_view name, bool quiet, Function function,
                  Args... args) {
  if (formatting_) {
    return function(args...);
  }
  BeginCall(quiet, FormatCall(name, args...));
  if constexpr (std::is_void_v<decltype(function(args...))>) {
    function(args...);
    if (!quiet) {
      track_names_.clear();
    }
    EndCall(name, {}, FormatOutputs(args...));
  } else {
    auto result = function(args...);
    if (!quiet) {
      track_names_.clear();
    }

    // Every REAPER function on the list that returns bool returns false when it
    // fails, and may leave its outputs unwritten, so they aren't read.
    bool wrote_outputs = true;
    if constexpr (std::is_same_v<decltype(result), bool>) {
      wrote_outputs = result;
    }
    EndCall(name, FormatResult(result, args...),
            wrote_outputs ? FormatOutputs(args...) : std::string());
    return result;
  }
}

int Tracer::CallExtended(IReaperControlSurface& surface, int call, void* param1,
                         void* param2, void* param3) {
  BeginCall(/*quiet=*/false, FormatExtended(call, param1, param2, param3));
  const int result = surface.Extended(call, param1, param2, param3);
  track_names_.clear();
  EndCall("csurf/Extended", FormatValue(result), {});
  return result;
}

void Tracer::BeginCall(bool quiet, std::string call) {
  if (depth_ == 0) {
    call_time_ = absl::Now();
  }
  if (!quiet) {
    has_event_ = true;
    track_names_.clear();
  }
  if (!pending_.empty()) {
    AddLine(depth_ - 1, pending_);
  }
  pending_ = std::move(call);
  ++depth_;
}

void Tracer::EndCall(std::string_view name, std::string_view result,
                     std::string_view outputs) {
  --depth_;
  std::string suffix;
  if (!result.empty() || !outputs.empty()) {
    suffix = absl::StrCat(" -> ", result,
                          !result.empty() && !outputs.empty() ? "; " : "",
                          outputs.empty() ? "" : "out: ", outputs);
  }
  if (!pending_.empty()) {
    AddLine(depth_, absl::StrCat(pending_, suffix));
    pending_.clear();
  } else if (!suffix.empty()) {
    AddLine(depth_, absl::StrCat(name, suffix));
  }
  if (depth_ == 0) {
    EndTopLevelCall();
  }
}

void Tracer::AddLine(int depth, std::string_view text) {
  if (depth == 0) {
    absl::StrAppendFormat(&lines_, "%*.3f ", kTimeWidth - 1,
                          absl::ToDoubleSeconds(call_time_ - start_time_));
  } else {
    lines_.append(kTimeWidth + 2 * depth, ' ');
  }
  absl::StrAppend(&lines_, text, "\n");
}

void Tracer::EndTopLevelCall() {
  if (has_event_) {
    WriteQuietCount();
    file_ << lines_;
  } else {
    ++quiet_count_;
  }
  lines_.clear();
  has_event_ = false;
  track_names_.clear();
  if (call_time_ - flush_time_ >= kFlushInterval) {
    file_.flush();
    flush_time_ = call_time_;
  }
}

void Tracer::WriteQuietCount() {
  if (quiet_count_ == 0) {
    return;
  }
  file_ << std::string(kTimeWidth, ' ') << "(" << quiet_count_
        << " quiet calls left out)\n";
  quiet_count_ = 0;
}

template <typename... Args>
std::string Tracer::FormatCall(std::string_view name, Args... args) {
  const std::vector<std::string> formatted_args = {FormatArg(args)...};
  return absl::StrCat(name, "(", absl::StrJoin(formatted_args, ", "), ")");
}

template <typename... Args>
std::string Tracer::FormatOutputs(Args... args) {
  std::vector<std::string> outputs;
  (AppendOutput(outputs, args), ...);
  return absl::StrJoin(outputs, ", ");
}

template <typename Result, typename... Args>
std::string Tracer::FormatResult(Result result, Args... args) {
  return FormatValue(result);
}

// GetSetTrackSendInfo() returns a pointer whose type depends on `parmname`.
std::string Tracer::FormatResult(void* result, MediaTrack* track, int category,
                                 int send_index, const char* parmname,
                                 void* set_new_value) {
  const std::string_view parm = absl::NullSafeStringView(parmname);
  if (parm == "P_DESTTRACK" || parm == "P_SRCTRACK") {
    return FormatValue(static_cast<MediaTrack*>(result));
  }
  return FormatValue(static_cast<const void*>(result));
}

std::string Tracer::FormatExtended(int call, void* param1, void* param2,
                                   void* param3) {
  static constexpr ExtendedCall kUnknownCall = {};
  const ExtendedCall* extended_call = &kUnknownCall;
  for (const ExtendedCall& known_call : kExtendedCalls) {
    if (known_call.call == call) {
      extended_call = &known_call;
      break;
    }
  }
  std::vector<std::string> args = {extended_call->name != nullptr
                                       ? extended_call->name
                                       : absl::StrFormat("0x%08X", call)};
  void* const params[] = {param1, param2, param3};
  for (int i = 0; i < 3; ++i) {
    args.push_back(FormatExtendedParam(extended_call->params[i], params[i]));
  }
  return absl::StrCat("csurf/Extended(", absl::StrJoin(args, ", "), ")");
}

std::string Tracer::FormatExtendedParam(ExtendedParam param, void* value) {
  switch (param) {
    case ExtendedParam::kTrack:
      return FormatValue(static_cast<MediaTrack*>(value));
    case ExtendedParam::kInt:
      return FormatOutput(static_cast<int*>(value));
    case ExtendedParam::kDouble:
      return FormatOutput(static_cast<double*>(value));
    case ExtendedParam::kFlag:
      return FormatValue(value != nullptr);
    case ExtendedParam::kValue:
      return FormatValue(static_cast<int>(reinterpret_cast<intptr_t>(value)));
    case ExtendedParam::kPointer:
      break;
  }
  return FormatValue(static_cast<const void*>(value));
}

template <typename Arg>
std::string Tracer::FormatArg(Arg arg) {
  if constexpr (kIsOutput<Arg>) {
    return "out";
  } else {
    return FormatValue(arg);
  }
}

template <typename Arg>
void Tracer::AppendOutput(std::vector<std::string>& outputs, Arg arg) {
  if constexpr (kIsOutput<Arg>) {
    outputs.push_back(FormatOutput(arg));
  }
}

std::string Tracer::FormatOutput(char* value) {
  return FormatValue(static_cast<const char*>(value));
}

template <typename T>
std::string Tracer::FormatOutput(T* value) {
  if (value == nullptr) {
    return "null";
  }
  return FormatValue(*value);
}

std::string Tracer::FormatValue(bool value) { return value ? "true" : "false"; }

std::string Tracer::FormatValue(int value) { return absl::StrCat(value); }

std::string Tracer::FormatValue(double value) { return absl::StrCat(value); }

std::string Tracer::FormatValue(const char* value) {
  if (value == nullptr) {
    return "null";
  }
  return absl::StrCat("\"", absl::CHexEscape(value), "\"");
}

std::string Tracer::FormatValue(const GUID& value) {
  return absl::StrFormat("{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                         value.Data1, value.Data2, value.Data3, value.Data4[0],
                         value.Data4[1], value.Data4[2], value.Data4[3],
                         value.Data4[4], value.Data4[5], value.Data4[6],
                         value.Data4[7]);
}

std::string Tracer::FormatValue(const GUID* value) {
  if (value == nullptr) {
    return "null";
  }
  return FormatValue(*value);
}

std::string Tracer::FormatValue(MediaTrack* value) {
  if (value == nullptr) {
    return "null";
  }

  auto [it, added] = track_names_.try_emplace(value);
  if (added) {
    it->second = NameTrack(value);
  }
  return it->second;
}

std::string Tracer::NameTrack(MediaTrack* value) {
  formatting_ = true;
  const int number =
      static_cast<int>(GetMediaTrackInfo_Value(value, "IP_TRACKNUMBER"));
  char name[256] = "";
  if (number > 0) {
    GetSetMediaTrackInfo_String(value, "P_NAME", name, /*setNewValue=*/false);
  }
  formatting_ = false;

  if (number == -1) {
    return "master";
  }
  if (number == 0) {
    return absl::StrCat("unknown track ",
                        FormatValue(static_cast<void*>(value)));
  }
  return absl::StrCat("track ", number, " ", FormatValue(name));
}

std::string Tracer::FormatValue(const void* value) {
  if (value == nullptr) {
    return "null";
  }
  return absl::StrFormat("%p", value);
}

//==============================================================================
// Hooks on the REAPER API and the control surface
//==============================================================================

// The trace, while one exists.
Tracer* g_tracer = nullptr;

// Traces `name`, while a trace exists.
template <typename Function, typename... Args>
auto TraceCall(std::string_view name, bool quiet, Function function,
               Args... args) {
  if (g_tracer == nullptr) {
    return function(args...);
  }
  return g_tracer->Call(name, quiet, function, args...);
}

class TraceHook final {
 public:
  explicit TraceHook(std::string_view name)
      : name_(name), quiet_(IsQuiet(name)) {}

  template <typename Function, typename... Args>
  auto Call(Function original, Args... args) {
    return TraceCall(name_, quiet_, original, args...);
  }

 private:
  const std::string_view name_;
  const bool quiet_;
};

// The registration that TraceSurfaceRegistration() traced, and what REAPER is
// given in its place.
reaper_csurf_reg_t* g_surface_reg = nullptr;
reaper_csurf_reg_t g_traced_surface_reg = {};

// The name and member of an IReaperControlSurface method, for CallSurface().
#define JPR_SURFACE_METHOD(method) \
  "csurf/" #method, &IReaperControlSurface::method

// Wraps a control surface, and traces every call REAPER makes on it.
class TracedSurface final : public IReaperControlSurface {
 public:
  explicit TracedSurface(IReaperControlSurface* surface) : surface_(surface) {}
  TracedSurface(const TracedSurface&) = delete;
  TracedSurface& operator=(const TracedSurface&) = delete;
  ~TracedSurface() override {
    TraceCall("csurf/Destroy", /*quiet=*/false, [this] { surface_.reset(); });
  }

  const char* GetTypeString() override {
    return CallSurface(JPR_SURFACE_METHOD(GetTypeString));
  }
  const char* GetDescString() override {
    return CallSurface(JPR_SURFACE_METHOD(GetDescString));
  }
  const char* GetConfigString() override {
    return CallSurface(JPR_SURFACE_METHOD(GetConfigString));
  }
  void CloseNoReset() override {
    CallSurface(JPR_SURFACE_METHOD(CloseNoReset));
  }
  void Run() override { CallSurface(JPR_SURFACE_METHOD(Run)); }
  void SetTrackListChange() override {
    CallSurface(JPR_SURFACE_METHOD(SetTrackListChange));
  }
  void SetSurfaceVolume(MediaTrack* track, double volume) override {
    CallSurface(JPR_SURFACE_METHOD(SetSurfaceVolume), track, volume);
  }
  void SetSurfacePan(MediaTrack* track, double pan) override {
    CallSurface(JPR_SURFACE_METHOD(SetSurfacePan), track, pan);
  }
  void SetSurfaceMute(MediaTrack* track, bool mute) override {
    CallSurface(JPR_SURFACE_METHOD(SetSurfaceMute), track, mute);
  }
  void SetSurfaceSelected(MediaTrack* track, bool selected) override {
    CallSurface(JPR_SURFACE_METHOD(SetSurfaceSelected), track, selected);
  }
  void SetSurfaceSolo(MediaTrack* track, bool solo) override {
    CallSurface(JPR_SURFACE_METHOD(SetSurfaceSolo), track, solo);
  }
  void SetSurfaceRecArm(MediaTrack* track, bool rec_arm) override {
    CallSurface(JPR_SURFACE_METHOD(SetSurfaceRecArm), track, rec_arm);
  }
  void SetPlayState(bool play, bool pause, bool rec) override {
    CallSurface(JPR_SURFACE_METHOD(SetPlayState), play, pause, rec);
  }
  void SetRepeatState(bool repeat) override {
    CallSurface(JPR_SURFACE_METHOD(SetRepeatState), repeat);
  }
  void SetTrackTitle(MediaTrack* track, const char* title) override {
    CallSurface(JPR_SURFACE_METHOD(SetTrackTitle), track, title);
  }
  bool GetTouchState(MediaTrack* track, int is_pan) override {
    return CallSurface(JPR_SURFACE_METHOD(GetTouchState), track, is_pan);
  }
  void SetAutoMode(int mode) override {
    CallSurface(JPR_SURFACE_METHOD(SetAutoMode), mode);
  }
  void ResetCachedVolPanStates() override {
    CallSurface(JPR_SURFACE_METHOD(ResetCachedVolPanStates));
  }
  void OnTrackSelection(MediaTrack* track) override {
    CallSurface(JPR_SURFACE_METHOD(OnTrackSelection), track);
  }
  bool IsKeyDown(int key) override {
    return CallSurface(JPR_SURFACE_METHOD(IsKeyDown), key);
  }
  int Extended(int call, void* param1, void* param2, void* param3) override {
    if (g_tracer == nullptr) {
      return surface_->Extended(call, param1, param2, param3);
    }
    return g_tracer->CallExtended(*surface_, call, param1, param2, param3);
  }

 private:
  template <typename Method, typename... Args>
  auto CallSurface(std::string_view name, Method method, Args... args) {
    return TraceCall(
        name, IsQuiet(name),
        [this, method](Args... call_args) {
          return (surface_.get()->*method)(call_args...);
        },
        args...);
  }

  std::unique_ptr<IReaperControlSurface> surface_;
};

#undef JPR_SURFACE_METHOD

IReaperControlSurface* CreateTracedSurface(const char* type_string,
                                           const char* config_string,
                                           int* err_stats) {
  IReaperControlSurface* surface =
      TraceCall("csurf/Create", /*quiet=*/false, g_surface_reg->create,
                type_string, config_string, err_stats);
  if (surface == nullptr || g_tracer == nullptr) {
    return surface;
  }
  return new TracedSurface(surface);
}

HWND ShowTracedConfig(const char* type_string, HWND parent,
                      const char* init_config_string) {
  return TraceCall("csurf/ShowConfig", /*quiet=*/false,
                   g_surface_reg->ShowConfig, type_string, parent,
                   init_config_string);
}

}  // namespace

//==============================================================================
// ReaperTrace
//==============================================================================

// The hooks are declared after the tracer, so they are removed before it is
// destroyed.
class ReaperTrace::Impl final {
 public:
  explicit Impl(const std::filesystem::path& path) : tracer(path) {}

  Tracer tracer;
  ReaperApiHooks<TraceHook> hooks;
};

ReaperTrace::ReaperTrace(const std::filesystem::path& path) {
  CHECK(g_tracer == nullptr) << "Only one ReaperTrace may exist at a time";
  impl_ = std::make_unique<Impl>(path);
  g_tracer = &impl_->tracer;
}

ReaperTrace::~ReaperTrace() { g_tracer = nullptr; }

reaper_csurf_reg_t* ReaperTrace::TraceSurfaceRegistration(
    reaper_csurf_reg_t* reg) {
  if (g_tracer == nullptr) {
    return reg;
  }
  CHECK(g_surface_reg == nullptr || g_surface_reg == reg)
      << "Only one control surface type can be traced";
  g_surface_reg = reg;
  g_traced_surface_reg = {
      reg->type_string, reg->desc_string, &CreateTracedSurface,
      reg->ShowConfig != nullptr ? &ShowTracedConfig : nullptr};
  return &g_traced_surface_reg;
}

}  // namespace jpr
