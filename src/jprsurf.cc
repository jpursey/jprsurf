// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <iostream>
#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "gb/base/context_builder.h"
#include "gb/file/file_system.h"
#include "gb/file/local_file_protocol.h"

namespace jpr {

bool Main(absl::Span<const std::string_view> args) {
  if (args.size() != 1) {
    std::cerr << "Usage: jprsurf <command> <args...>" << std::endl;
    return false;
  }

  gb::FileSystem file_system;
  file_system.Register(gb::LocalFileProtocol::Create(
      gb::ContextBuilder().SetValue<std::string>("root", "/").Build()));
  file_system.SetDefaultProtocol("file");

  return true;
}

}  // namespace jpr

int main(int argc, const char* argv[]) {
  std::vector<std::string_view> args(argc - 1);
  for (int i = 1; i < argc; ++i) {
    args[i - 1] = argv[i];
  }
  return jpr::Main(args) ? 0 : -1;
}