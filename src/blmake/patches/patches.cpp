#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <MinHook.h>

#include <atomic>

#include "blmake/logging.h"
#include "blmake/patches/delegate_import.h"
#include "blmake/patches/make_commandlet.h"
#include "blmake/patches/patches.h"
#include "blmake/patches/stale_defaults.h"

namespace blmake {

namespace {

using IsDebuggerPresentFn = BOOL(WINAPI*)();

IsDebuggerPresentFn fn_is_debugger_present = nullptr;

std::atomic patches_applied{false};
bool no_compress = false;

////////////////////////////////////////////////////////////////////////////////
// | ENTRY |
////////////////////////////////////////////////////////////////////////////////

bool try_install() {
    if (!install_make_commandlet_patches(no_compress)) {
        return false;
    }

    if (!install_stale_defaults_purge()) {
        BLMAKE_LOG("couldn't find the functions needed to purge stale default subobjects");
    }

    if (!install_delegate_import_fix()) {
        BLMAKE_LOG("couldn't install the delegate import fix");
    }

    return true;
}

BOOL WINAPI is_debugger_present_hook() {
    if (!patches_applied.load()) {
        BLMAKE_LOG("attempting make commandlet patches...");
        if (try_install()) {
            patches_applied.store(true);
            BLMAKE_LOG("all patches applied successfully");
        }
    }
    return fn_is_debugger_present();
}

bool is_make_run() {
    int argc{0};
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &argc);

    const auto contains_icase = [&](const wchar_t* arg) {
        for (int i = 2; i < argc; ++i) {
            if (_wcsicmp(args[i], arg) == 0) {
                return true;
            }
        }
        return false;
    };

    // positional
    const bool has_make_arg = argc > 1 && _wcsnicmp(args[1], L"make", 4) == 0;
    no_compress = contains_icase(L"-nocompress");

    LocalFree(static_cast<void*>(args));
    return has_make_arg;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// | ENTRY |
////////////////////////////////////////////////////////////////////////////////

void install_patches() {
    if (!is_make_run()) {
        return;
    }
    BLMAKE_LOG("Make commandlet detected - awaiting exe decryption");

    void* target = nullptr;

    const auto ret = MH_CreateHookApiEx(
        L"kernel32",
        "IsDebuggerPresent",
        reinterpret_cast<void*>(&is_debugger_present_hook),
        reinterpret_cast<void**>(&fn_is_debugger_present),
        &target
    );

    if (ret != MH_OK || MH_EnableHook(target) != MH_OK) {
        BLMAKE_LOG("failed to hook IsDebuggerPresent: {}", MH_StatusToString(ret));
    }
}

}  // namespace blmake
