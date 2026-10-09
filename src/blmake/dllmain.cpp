#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <MinHook.h>

#include <filesystem>

#include "logging.h"
#include "patches.h"

namespace fs = std::filesystem;

// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
BOOL APIENTRY DllMain(HMODULE hinst_dll, DWORD fdw_reason, LPVOID /*lpv_reserved*/) {
    switch (fdw_reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hinst_dll);

            wchar_t module_path[MAX_PATH];
            if (GetModuleFileNameW(hinst_dll, module_path, MAX_PATH) != 0) {
                blmake::setup_logging(fs::path{module_path}.replace_filename(L"blmake.log"));
            }

            if (MH_Initialize() != MH_OK) {
                BLMAKE_LOG("MinHook failed to initialise");
                return FALSE;
            }

            blmake::install_patches();
            break;
        }

        case DLL_PROCESS_DETACH: {
            MH_Uninitialize();
            blmake::shutdown_logging();
            break;
        }

        default: {
            break;
        }
    }
    return TRUE;
}
