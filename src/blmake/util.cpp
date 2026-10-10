#include "blmake/util.h"
#include "blmake/logging.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <MinHook.h>

#include <cstring>

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

hat::scan_result find_static_find_object() {
    static hat::scan_result cached;
    if (cached.has_result()) {
        return cached;
    }

    constexpr auto signature = hat::compile_signature<
        " 6A FF"              // push -1
        " 68 ?? ?? ?? ??"     // push handler
        " 64 A1 ?? ?? ?? ??"  // mov eax, fs:[0]
        " 50"                 // push eax
        " 83 EC 24"           // sub esp, 0x24
        " 53"                 // push ebx
        " 55"                 // push ebp
        " 56"                 // push esi
        " 57"                 // push edi
        " A1 ?? ?? ?? ??"     // mov eax, [security_cookie]
        " 33 C4"              // xor eax, esp
        " 50"                 // push eax
        " 8D 44 24 ??"        // lea eax, [esp + 0x38]
        " 64 A3 ?? ?? ?? ??"  // mov fs:[0], eax
        " 8B 74 24 ??"        // mov esi, [esp + 0x4c]
        " 8B 7C 24 ??"        // mov edi, [esp + 0x50]
        >();
    cached = hat::find_pattern(signature, ".text");
    return cached;
}

hat::scan_result find_rename() {
    // UBOOL UObject::Rename(const TCHAR* NewName, UObject* NewOuter, ERenameFlags Flags)
    constexpr auto signature = hat::compile_signature<
        " 6A FF"                    // push -1
        " 68 ?? ?? ?? ??"           // push handler
        " 64 A1 00 00 00 00"        // mov eax, fs:[0]
        " 50"                       // push eax
        " 83 EC 2C"                 // sub esp, 0x2c
        " 53"                       // push ebx
        " 55"                       // push ebp
        " 56"                       // push esi
        " 57"                       // push edi
        " A1 ?? ?? ?? ??"           // mov eax, [security_cookie]
        " 33 C4"                    // xor eax, esp
        " 50"                       // push eax
        " 8D 44 24 40"              // lea eax, [esp + 0x40]
        " 64 A3 00 00 00 00"        // mov fs:[0], eax
        " 8B F1"                    // mov esi, ecx
        " 8B 6C 24 54"              // mov ebp, [esp + 0x54] (NewOuter)
        " C7 44 24 14 00 00 00 00"  // mov [esp + 0x14], 0
        " 85 ED"                    // test ebp, ebp
        " 0F 84 ?? ?? ?? ??"        // jz no_outer_check
        " 8B 4E 34"                 // mov ecx, [esi + 0x34] (Class)
        >();
    return hat::find_pattern(signature, ".text");
}

TArray<UObject*>* find_gobjects() {
    constexpr auto signature = hat::compile_signature<
        " 8B 0D ?? ?? ?? ??"  // mov ecx, [GObjObjects]
        " 8B 04 ??"           // mov eax, [ecx + esi*4]
        " 8B 50 ??"           // mov edx, [eax + 0xc]
        " 21 58 ??"           // and [eax + 8], ebx
        " 89 50 ??"           // mov [eax + 0xc], edx
        >();

    const auto result = hat::find_pattern(signature, ".text");
    if (!result.has_result()) {
        return nullptr;
    }
    TArray<UObject*>* gobjects = nullptr;
    std::memcpy(static_cast<void*>(&gobjects), result.get() + 2, sizeof(decltype(gobjects)));
    return gobjects;
}

UObject* find_object(const wchar_t* path) {
    const auto result = find_static_find_object();

    if (!result.has_result()) {
        return nullptr;
    }

    return reinterpret_cast<StaticFindObjectFn>(result.get())(nullptr, nullptr, path, 0);
}

// TODO: rename to is_or_has_parent
// NOLINTNEXTLINE(*-easily-swappable-parameters)
bool is_a(const UObject* obj, const UClass* cls) {
    for (const UStruct* it = obj->Class; it != nullptr; it = it->SuperField) {
        if (it == cls) {
            return true;
        }
    }
    return false;
}

// TODO: rename to is_child_of
// NOLINTNEXTLINE(*-easily-swappable-parameters)
bool is_inside(const UObject* obj, const UObject* outer) {
    for (const UObject* it = obj->Outer; it != nullptr; it = it->Outer) {
        if (it == outer) {
            return true;
        }
    }
    return false;
}

}  // namespace blmake