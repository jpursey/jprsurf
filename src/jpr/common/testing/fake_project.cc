// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/fake_project.h"

namespace jpr {

FakeProject::FakeProject(int number) : number_(number) {
  master_track_.guid = MakeGuid();
  master_track_.name = "MASTER";
}

GUID FakeProject::MakeGuid() {
  GUID guid = {};
  guid.Data1 = next_guid_++;
  guid.Data2 = static_cast<unsigned short>(number_);
  return guid;
}

}  // namespace jpr
