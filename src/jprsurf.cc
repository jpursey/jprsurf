// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <windows.h>

#include <string>

#include "gb/base/context_builder.h"
#include "gb/file/file_system.h"
#include "gb/file/local_file_protocol.h"

namespace jpr {

void Attach() {
  gb::FileSystem file_system;
  file_system.Register(gb::LocalFileProtocol::Create(
      gb::ContextBuilder().SetValue<std::string>("root", "/").Build()));
  file_system.SetDefaultProtocol("file");
}

}  // namespace jpr

BOOL APIENTRY DllMain(HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        jpr::Attach();
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        // Cleanup code goes here
        break;
    }
    return TRUE;
}
