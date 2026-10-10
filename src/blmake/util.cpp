#include "blmake/util.h"
#include "blmake/logging.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <MinHook.h>

namespace blmake {

bool write_code(std::byte* at, const uint8_t* bytes, size_t size) {
    constexpr auto flags = hat::protection::Read | hat::protection::Write | hat::protection::Execute;
    const hat::memory_protector protect{reinterpret_cast<uintptr_t>(at), size, flags};

    if (!protect.is_set()) {
        return false;
    }

    std::memcpy(at, bytes, size);

    // flush that mf toilet bruh
    FlushInstructionCache(GetCurrentProcess(), at, size);

    return true;
}

bool apply_patch(
    const char* name,
    hat::scan_result result,
    size_t offset,
    const uint8_t* bytes,
    size_t size
) {
    if (!result.has_result()) {
        return false;
    }

    std::byte* at = result.get() + offset;
    if (!write_code(at, bytes, size)) {
        BLMAKE_LOG("{}: couldn't write {} bytes at {}", name, size, static_cast<void*>(at));
        return false;
    }
    BLMAKE_LOG("{}: patched at {}", name, static_cast<void*>(at));
    return true;
}

bool install_hook(const char* name, hat::scan_result result, void* detour, void** original) {
    if (!result.has_result()) {
        return false;
    }
    void* target = result.get();

    const auto hook_ret = MH_CreateHook(target, detour, original);
    if (hook_ret != MH_OK || MH_EnableHook(target) != MH_OK) {
        BLMAKE_LOG("couldn't hook {} at {}: {}", name, target, MH_StatusToString(hook_ret));
        return false;
    }

    BLMAKE_LOG("hooked {} at {}", name, target);
    return true;
}

}  // namespace blmake