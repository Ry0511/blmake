#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <MinHook.h>
#include <libhat.hpp>

#include <atomic>
#include <cstddef>
#include <cstring>

#include "logging.h"
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
using CopyInheritedComponentsFn = void(__cdecl*)(UClass* cls, void* instance_graph);
using IsDebuggerPresentFn = BOOL(WINAPI*)();

ParseScriptsFn fn_parse_scripts = nullptr;
CopyInheritedComponentsFn fn_copy_inherited_components = nullptr;
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
    return apply_patch(
        __func__,
        hat::find_pattern(signature, ".text"),
        offset,
        bytes,
        sizeof bytes
    );
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
    return apply_patch(
        __func__,
        hat::find_pattern(signature, ".text"),
        offset,
        bytes,
        sizeof bytes
    );
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
    return apply_patch(
        __func__,
        hat::find_pattern(signature, ".text"),
        offset,
        bytes,
        sizeof bytes
    );
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
    if (cls != nullptr) {
        // the engine will not set these flags for cooked packages so we need to do it
        if (cls->ScriptText == nullptr) {
            cls->ClassFlags |= CLASS_Parsed | CLASS_Compiled;
        }

        // when recompiling a compiled package i.e., Core.u or WillowGame.u then some of our prior
        // patches actually cause problems (ironic) so if the class is not compiled but has native
        // functions bound to it, then we need to clear them first.
        else if ((cls->ClassFlags & CLASS_Compiled) != 0) {
            cls->ClassFlags &= ~(CLASS_Parsed | CLASS_Compiled);
            auto& funcs = cls->FuncMap.Pairs;
            for (int32_t i = 0; i < funcs.max_index(); ++i) {
                if (funcs.is_allocated(i) && funcs.at(i).Value != nullptr) {
                    funcs.at(i).Value->iNative = 0;
                }
            }
        }
    }
    return fn_parse_scripts(tree, compiler, cls, make_all, booting, make_subclasses);
}

int remove_null_component_templates(TMap<FName, UObject*>& map) {
    auto& pairs = map.Pairs;
    int removed = 0;

    for (int32_t i = 0; i < pairs.max_index(); ++i) {
        if (pairs.is_allocated(i) && pairs.at(i).Value == nullptr) {
            pairs.remove(i);
            ++removed;
        }
    }
    return removed;
}

// ::ImportProperties helper that copies components into its defaults maps
void __cdecl copy_inherited_components_hook(UClass* cls, void* instance_graph) {
    // null pointers do be quite dangerous
    if (cls != nullptr) {
        const int removed = remove_null_component_templates(cls->ComponentNameToDefaultObjectMap);
        if (removed > 0) {
            BLMAKE_LOG(
                "dropped {} null component template(s) from the class at {}",
                removed,
                static_cast<void*>(cls)
            );
        }
    }
    fn_copy_inherited_components(cls, instance_graph);
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

    return install_hook(
        "ParseScripts",
        hat::find_pattern(signature, ".text"),
        reinterpret_cast<void*>(&parse_scripts_hook),
        reinterpret_cast<void**>(&fn_parse_scripts)
    );
}

bool hook_copy_inherited_components() {
    constexpr auto signature = hat::compile_signature<
        " 6A FF"                 // push -1
        " 68 ?? ?? ?? ??"        // push handler
        " 64 A1 00 00 00 00"     // mov eax, fs:[0]
        " 50"                    // push eax
        " 81 EC A4 01 00 00"     // sub esp, 0x1a4
        " 53"                    // push ebx
        " 55"                    // push ebp
        " 56"                    // push esi
        " 57"                    // push edi
        " A1 ?? ?? ?? ??"        // mov eax, [security_cookie]
        " 33 C4"                 // xor eax, esp
        " 50"                    // push eax
        " 8D 84 24 B8 01 00 00"  // lea eax, [esp + 0x1b8]
        " 64 A3 00 00 00 00"     // mov fs:[0], eax
        " 33 FF"                 // xor edi, edi
        " 89 BC 24 88 00 00 00"  // mov [esp + 0x88], edi
        >();

    return install_hook(
        "CopyInheritedComponents",
        hat::find_pattern(signature, ".text"),
        reinterpret_cast<void*>(&copy_inherited_components_hook),
        reinterpret_cast<void**>(&fn_copy_inherited_components)
    );
}

bool try_install() {
    return patch_superclass_check()
           && patch_parent_parsed_check()
           && patch_parse_scripts_early_out()
           && hook_parse_scripts()
           && hook_copy_inherited_components();
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
