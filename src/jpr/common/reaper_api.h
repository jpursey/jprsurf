// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

//==============================================================================
// The REAPER API functions JPRSurf calls.
//
// Include this header rather than the SDK's reaper_plugin_functions.h, which
// then declares only these functions, so calling any other doesn't compile.
//
// A function is added to both lists: a REAPERAPI_WANT_ line, which has the SDK
// declare and load it, and an entry in JPR_REAPER_API. A function missing from
// either one is caught, when compiling or by LoadReaperApi().
//==============================================================================

#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_AnyTrackSolo
#define REAPERAPI_WANT_CountSelectedMediaItems
#define REAPERAPI_WANT_CountSelectedTracks
#define REAPERAPI_WANT_CountSelectedTracks2
#define REAPERAPI_WANT_CountTracks
#define REAPERAPI_WANT_CreateMIDIInput
#define REAPERAPI_WANT_CreateMIDIOutput
#define REAPERAPI_WANT_CSurf_OnPanChangeEx
#define REAPERAPI_WANT_CSurf_OnVolumeChangeEx
#define REAPERAPI_WANT_format_timestr_pos
#define REAPERAPI_WANT_GetCursorPosition
#define REAPERAPI_WANT_GetGlobalAutomationOverride
#define REAPERAPI_WANT_GetMasterTrack
#define REAPERAPI_WANT_GetMediaTrackInfo_Value
#define REAPERAPI_WANT_GetMIDIInputName
#define REAPERAPI_WANT_GetMIDIOutputName
#define REAPERAPI_WANT_GetNumMIDIInputs
#define REAPERAPI_WANT_GetNumMIDIOutputs
#define REAPERAPI_WANT_GetParentTrack
#define REAPERAPI_WANT_GetPlayPosition
#define REAPERAPI_WANT_GetPlayState
#define REAPERAPI_WANT_GetSelectedTrack
#define REAPERAPI_WANT_GetSelectedTrack2
#define REAPERAPI_WANT_GetSetMediaTrackInfo_String
#define REAPERAPI_WANT_GetSetTrackSendInfo
#define REAPERAPI_WANT_GetToggleCommandState
#define REAPERAPI_WANT_GetTrack
#define REAPERAPI_WANT_GetTrackColor
#define REAPERAPI_WANT_GetTrackGUID
#define REAPERAPI_WANT_GetTrackNumSends
#define REAPERAPI_WANT_GetTrackReceiveUIMute
#define REAPERAPI_WANT_GetTrackReceiveUIVolPan
#define REAPERAPI_WANT_GetTrackSendUIMute
#define REAPERAPI_WANT_GetTrackSendUIVolPan
#define REAPERAPI_WANT_GetTrackState
#define REAPERAPI_WANT_GetTrackUIVolPan
#define REAPERAPI_WANT_guidToString
#define REAPERAPI_WANT_IsProjectDirty
#define REAPERAPI_WANT_kbd_getTextFromCmd
#define REAPERAPI_WANT_Main_OnCommand
#define REAPERAPI_WANT_mkpanstr
#define REAPERAPI_WANT_mkvolstr
#define REAPERAPI_WANT_NamedCommandLookup
#define REAPERAPI_WANT_PreventUIRefresh
#define REAPERAPI_WANT_SetGlobalAutomationOverride
#define REAPERAPI_WANT_SetOnlyTrackSelected
#define REAPERAPI_WANT_SetTrackSelected
#define REAPERAPI_WANT_SetTrackSendUIPan
#define REAPERAPI_WANT_SetTrackSendUIVol
#define REAPERAPI_WANT_SetTrackUIMute
#define REAPERAPI_WANT_SetTrackUIRecArm
#define REAPERAPI_WANT_SetTrackUISolo
#define REAPERAPI_WANT_ShowConsoleMsg
#define REAPERAPI_WANT_stringToGuid
#define REAPERAPI_WANT_time_precise
#define REAPERAPI_WANT_ToggleTrackSendUIMute
#define REAPERAPI_WANT_Track_GetPeakInfo
#define REAPERAPI_WANT_Undo_CanRedo2
#define REAPERAPI_WANT_Undo_OnStateChangeEx

#include "sdk/reaper_plugin_functions.h"

// Every REAPER API function JPRSurf calls. X(name) is expanded for each one.
#define JPR_REAPER_API(X)        \
  X(AnyTrackSolo)                \
  X(CountSelectedMediaItems)     \
  X(CountSelectedTracks)         \
  X(CountSelectedTracks2)        \
  X(CountTracks)                 \
  X(CreateMIDIInput)             \
  X(CreateMIDIOutput)            \
  X(CSurf_OnPanChangeEx)         \
  X(CSurf_OnVolumeChangeEx)      \
  X(format_timestr_pos)          \
  X(GetCursorPosition)           \
  X(GetGlobalAutomationOverride) \
  X(GetMasterTrack)              \
  X(GetMediaTrackInfo_Value)     \
  X(GetMIDIInputName)            \
  X(GetMIDIOutputName)           \
  X(GetNumMIDIInputs)            \
  X(GetNumMIDIOutputs)           \
  X(GetParentTrack)              \
  X(GetPlayPosition)             \
  X(GetPlayState)                \
  X(GetSelectedTrack)            \
  X(GetSelectedTrack2)           \
  X(GetSetMediaTrackInfo_String) \
  X(GetSetTrackSendInfo)         \
  X(GetToggleCommandState)       \
  X(GetTrack)                    \
  X(GetTrackColor)               \
  X(GetTrackGUID)                \
  X(GetTrackNumSends)            \
  X(GetTrackReceiveUIMute)       \
  X(GetTrackReceiveUIVolPan)     \
  X(GetTrackSendUIMute)          \
  X(GetTrackSendUIVolPan)        \
  X(GetTrackState)               \
  X(GetTrackUIVolPan)            \
  X(guidToString)                \
  X(IsProjectDirty)              \
  X(kbd_getTextFromCmd)          \
  X(Main_OnCommand)              \
  X(mkpanstr)                    \
  X(mkvolstr)                    \
  X(NamedCommandLookup)          \
  X(PreventUIRefresh)            \
  X(SetGlobalAutomationOverride) \
  X(SetOnlyTrackSelected)        \
  X(SetTrackSelected)            \
  X(SetTrackSendUIPan)           \
  X(SetTrackSendUIVol)           \
  X(SetTrackUIMute)              \
  X(SetTrackUIRecArm)            \
  X(SetTrackUISolo)              \
  X(ShowConsoleMsg)              \
  X(stringToGuid)                \
  X(time_precise)                \
  X(ToggleTrackSendUIMute)       \
  X(Track_GetPeakInfo)           \
  X(Undo_CanRedo2)               \
  X(Undo_OnStateChangeEx)

namespace jpr {

// Loads the REAPER API from `get_func`, which is REAPER's GetFunc or a fake's.
//
// Returns false, and logs the names, if any function is missing from
// `get_func`, or has a REAPERAPI_WANT_ line but isn't in JPR_REAPER_API.
bool LoadReaperApi(void* (*get_func)(const char* name));

}  // namespace jpr
