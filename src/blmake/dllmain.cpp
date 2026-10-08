#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <MinHook.h>

#include "patches.h"

// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
BOOL APIENTRY DllMain(HMODULE hinst_dll, DWORD fdw_reason, LPVOID /*lpv_reserved*/) {
    switch (fdw_reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hinst_dll);
            if (MH_Initialize() != MH_OK) {
                return FALSE;
            }
            blmake::install_patches();
            break;
        }

        case DLL_PROCESS_DETACH: {
            MH_Uninitialize();
            break;
        }

        default: {
            break;
        }
    }
    return TRUE;
}
