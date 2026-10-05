// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/test_control_surface.h"

#include "absl/time/time.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {

TestControlSurface::TestControlSurface(FakeReaper* reaper,
                                       IReaperControlSurface* surface)
    : reaper_(reaper), surface_(surface) {}

TestControlSurface::~TestControlSurface() { Close(); }

void TestControlSurface::Close() {
  if (reaper_ == nullptr) {
    return;
  }
  surface_.reset();
  EndCall();
  reaper_->RemoveSurface(this);
  reaper_ = nullptr;
}

void TestControlSurface::EndCall() { reaper_->EndEntryPoint(); }

//------------------------------------------------------------------------------
// Running
//------------------------------------------------------------------------------

void TestControlSurface::Run() {
  reaper_->AdvanceRun();
  surface_->Run();
  EndCall();
}

void TestControlSurface::RunFor(absl::Duration duration) {
  const int runs = FakeReaper::GetRunCount(duration);
  for (int i = 0; i < runs; ++i) {
    Run();
  }
}

//------------------------------------------------------------------------------
// The rest of IReaperControlSurface
//------------------------------------------------------------------------------

const char* TestControlSurface::GetTypeString() {
  const char* result = surface_->GetTypeString();
  EndCall();
  return result;
}

const char* TestControlSurface::GetDescString() {
  const char* result = surface_->GetDescString();
  EndCall();
  return result;
}

const char* TestControlSurface::GetConfigString() {
  const char* result = surface_->GetConfigString();
  EndCall();
  return result;
}

void TestControlSurface::CloseNoReset() {
  surface_->CloseNoReset();
  EndCall();
}

void TestControlSurface::SetTrackListChange() {
  surface_->SetTrackListChange();
  EndCall();
}

void TestControlSurface::SetSurfaceVolume(MediaTrack* track, double volume) {
  surface_->SetSurfaceVolume(track, volume);
  EndCall();
}

void TestControlSurface::SetSurfacePan(MediaTrack* track, double pan) {
  surface_->SetSurfacePan(track, pan);
  EndCall();
}

void TestControlSurface::SetSurfaceMute(MediaTrack* track, bool mute) {
  surface_->SetSurfaceMute(track, mute);
  EndCall();
}

void TestControlSurface::SetSurfaceSelected(MediaTrack* track, bool selected) {
  surface_->SetSurfaceSelected(track, selected);
  EndCall();
}

void TestControlSurface::SetSurfaceSolo(MediaTrack* track, bool solo) {
  surface_->SetSurfaceSolo(track, solo);
  EndCall();
}

void TestControlSurface::SetSurfaceRecArm(MediaTrack* track, bool rec_arm) {
  surface_->SetSurfaceRecArm(track, rec_arm);
  EndCall();
}

void TestControlSurface::SetPlayState(bool play, bool pause, bool rec) {
  surface_->SetPlayState(play, pause, rec);
  EndCall();
}

void TestControlSurface::SetRepeatState(bool repeat) {
  surface_->SetRepeatState(repeat);
  EndCall();
}

void TestControlSurface::SetTrackTitle(MediaTrack* track, const char* title) {
  surface_->SetTrackTitle(track, title);
  EndCall();
}

bool TestControlSurface::GetTouchState(MediaTrack* track, int is_pan) {
  const bool result = surface_->GetTouchState(track, is_pan);
  EndCall();
  return result;
}

void TestControlSurface::SetAutoMode(int mode) {
  surface_->SetAutoMode(mode);
  EndCall();
}

void TestControlSurface::ResetCachedVolPanStates() {
  surface_->ResetCachedVolPanStates();
  EndCall();
}

void TestControlSurface::OnTrackSelection(MediaTrack* track) {
  surface_->OnTrackSelection(track);
  EndCall();
}

bool TestControlSurface::IsKeyDown(int key) {
  const bool result = surface_->IsKeyDown(key);
  EndCall();
  return result;
}

int TestControlSurface::Extended(int call, void* param1, void* param2,
                                 void* param3) {
  const int result = surface_->Extended(call, param1, param2, param3);
  EndCall();
  return result;
}

}  // namespace jpr
