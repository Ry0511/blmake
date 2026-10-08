#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <MinHook.h>
#include <libhat.hpp>

#include <atomic>
#include <cstddef>
#include <cstring>

#include "patches.h"
#include "structs.h"

namespace blmake {

////////////////////////////////////////////////////////////////////////////////
// | IMPL |
////////////////////////////////////////////////////////////////////////////////

namespace {

using ParseScriptsFn = int(__cdecl*)(
    void* tree,
    void* compiler,
    UClass* cls,
    int make_all,
    int booting,
    int make_subclasses
);
using IsDebuggerPresentFn = BOOL(WINAPI*)();

ParseScriptsFn fn_parse_scripts = nullptr;
IsDebuggerPresentFn fn_is_debugger_present = nullptr;
std::atomic patches_applied{false};

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

////////////////////////////////////////////////////////////////////////////////
// | PATCHES |
////////////////////////////////////////////////////////////////////////////////

// UMakeCommandlet::Main
bool patch_superclass_check() {
    // before MakeScripts runs, Main checks that every class with script text has an immediate
    // superclass with script text too. Classes from a cooked package do not have any script text
    // so that always fails.

    // for( TObjectIterator<UClass> ItC; ItC; ++ItC )
    // {
    //     const UClass* ScriptClass = *ItC;
    //     if( ScriptClass->ScriptText && ScriptClass->GetSuperClass() )
    //     {
    //         if( !ScriptClass->GetSuperClass()->ScriptText )
    //         {
    //             Success = FALSE; // nop nop
    //         }
    //     }
    // }

    constexpr auto signature = hat::compile_signature<
        " 8B 04 81"     // mov eax, [ecx + eax*4]
        " 83 78 44 00"  // cmp dword [eax + 0x44], 0
        " 74 0F"        // jz next
        " 8B 40 3C"     // mov eax, [eax + 0x3c]
        " 85 C0"        // test eax, eax
        " 74 08"        // jz next
        " 83 78 44 00"  // cmp dword [eax + 0x44], 0
        " 75 02"        // jnz next
        " ?? ??"        // xor esi, esi <- nop nop
        " 8D 8C 24"     // lea ecx, [esp + ...]
        >();

    constexpr auto offset = 22;
    constexpr uint8_t bytes[] = {0x90, 0x90};
    const auto result = hat::find_pattern(signature, ".text");
    return result.has_result() && write_code(result.get() + offset, bytes, sizeof bytes);
}

bool patch_parent_parsed_check() {
    //
    // Once again cooked packages are causing issues since the CLASS_Parsed flag is not set anymore.
    // ScriptErrorf just triggers a GPF.
    //
    // for (Parent = this->Class->SuperField; Parent != NULL; Parent = Parent->SuperField) {
    //     if ((Parent->ClassFlags & CLASS_Parsed) == 0) {
    //         ScriptErrorf("'%s' can't be compiled: Parent class '%s' has errors", ...); // skipped
    //     }
    // }
    constexpr auto signature = hat::compile_signature<
        " 8B 46 14"              // mov eax, [esi + 0x14]
        " 8B 40 3C"              // mov eax, [eax + 0x3c]
        " 89 45 0C"              // mov [ebp + 0xc], eax
        " 3B C3"                 // cmp eax, ebx
        " 0F 84 ?? ?? ?? ??"     // jz LAB_00f22bc2
        " 8D 49 00"              // lea ecx, [ecx]
        " F6 80 E8 00 00 00 10"  // test byte [eax + 0xe8], 0x10
        " ?? ?? ?? ?? ?? ??"     // jnz LAB_00f22bb4 <-- nop jmp LAB_00f22bb4
        >();
    constexpr auto offset = 27;
    constexpr uint8_t bytes[] = {0x90, 0xE9};
    const auto result = hat::find_pattern(signature, ".text");
    return result.has_result() && write_code(result.get() + offset, bytes, sizeof bytes);
}

bool patch_parse_scripts_early_out() {
    // This early exit causes mod packages to be skipped entirely since all classes derive Object
    // which Object.ScriptText == NULL since Core.u is cooked. We want our mod packages to be parsed
    // since the compile step depends upon it.
    //
    // if (Class->ScriptText == NULL) {
    //     return 1;  // skipped
    // }
    constexpr auto signature = hat::compile_signature<
        " 8B 6C 24 70"  // mov ebp, [esp + 0x70]
        " 33 DB"        // xor ebx, ebx
        " 39 5D 44"     // cmp [ebp + 0x44], ebx
        " ?? 17"        // jnz LAB_00F23549 <-- jmp LAB_00F23549
        " 8D 43 01"     // lea eax, [ebx + 1]
        " 8B 4C 24 58"  // mov ecx, [esp + 0x58]
        >();

    constexpr auto offset = 9;
    constexpr uint8_t bytes[] = {0xEB, 0x17};
    const auto result = hat::find_pattern(signature, ".text");
    return result.has_result() && write_code(result.get() + offset, bytes, sizeof bytes);
}

// static UBOOL ParseScripts
int __cdecl parse_scripts_hook(
    void* tree,
    void* compiler,
    UClass* cls,
    int make_all,
    int booting,
    int make_subclasses
) {
    // the engine will not set these flags for cooked packages so we need to do it
    if (cls != nullptr && cls->ScriptText == nullptr) {
        cls->ClassFlags |= CLASS_Parsed | CLASS_Compiled;
    }

    return fn_parse_scripts(tree, compiler, cls, make_all, booting, make_subclasses);
}

////////////////////////////////////////////////////////////////////////////////
// | HOOKS |
////////////////////////////////////////////////////////////////////////////////

bool hook_parse_scripts() {
    constexpr auto signature = hat::compile_signature<
        " 6A FF"              // push -1
        " 68 ?? ?? ?? ??"     // push handler
        " 64 A1 00 00 00 00"  // mov eax, fs:[0]
        " 50"                 // push eax
        " 83 EC 44"           // sub esp, 0x44
        " 53"                 // push ebx
        " 55"                 // push ebp
        " 56"                 // push esi
        " 57"                 // push edi
        " A1 ?? ?? ?? ??"     // mov eax, [security_cookie]
        " 33 C4"              // xor eax, esp
        " 50"                 // push eax
        " 8D 44 24 58"        // lea eax, [esp + 0x58]
        " 64 A3 00 00 00 00"  // mov fs:[0], eax
        " 8B 6C 24 70"        // mov ebp, [esp + 0x70]
        " 33 DB"              // xor ebx, ebx
        " 39 5D 44"           // cmp [ebp + 0x44], ebx
        >();

    const auto result = hat::find_pattern(signature, ".text");

    if (!result.has_result()) {
        return false;
    }

    void* target = result.get();

    const auto hook_ret = MH_CreateHook(
        target,
        reinterpret_cast<void*>(&parse_scripts_hook),
        reinterpret_cast<void**>(&fn_parse_scripts)
    );

    return hook_ret == MH_OK && MH_EnableHook(target) == MH_OK;
}

bool try_install() {
    return patch_superclass_check()
           && patch_parent_parsed_check()
           && patch_parse_scripts_early_out()
           && hook_parse_scripts();
}

BOOL WINAPI is_debugger_present_hook() {
    if (!patches_applied.load() && try_install()) {
        patches_applied.store(true);
    }
    return fn_is_debugger_present();
}

bool is_make_run() {
    int argc{0};
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool has_make_arg = argc > 1 && _wcsnicmp(args[1], L"make", 4) == 0;
    LocalFree(args);
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

    void* target = nullptr;

    const auto ret = MH_CreateHookApiEx(
        L"kernel32",
        "IsDebuggerPresent",
        reinterpret_cast<void*>(&is_debugger_present_hook),
        reinterpret_cast<void**>(&fn_is_debugger_present),
        &target
    );

    if (ret != MH_OK) {
        return;
    }
    MH_EnableHook(target);
}

}  // namespace blmake
