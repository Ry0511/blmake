#include <libhat.hpp>

#include "blmake/logging.h"
#include "blmake/patches/make_commandlet.h"
#include "blmake/patches/stale_defaults.h"
#include "blmake/structs.h"
#include "blmake/util.h"

namespace blmake {

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

ParseScriptsFn fn_parse_scripts = nullptr;
CopyInheritedComponentsFn fn_copy_inherited_components = nullptr;

bool no_compress = false;

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

#if BLMAKE_ENHANCED
    constexpr auto signature = hat::compile_signature<
        " 48 83 7B 68 00"     // cmp qword [rbx + 0x68], 0
        " 0F 84 ?? ?? ?? ??"  // jz next
        " 48 8B 43 78"        // mov rax, [rbx + 0x78]
        " 48 85 C0"           // test rax, rax
        " 0F 84 ?? ?? ?? ??"  // jz next
        " 48 83 78 68 00"     // cmp qword [rax + 0x68], 0
        " ?? ??"              // jnz next <- nop jmp next
        >();

    constexpr auto offset = 29;
    constexpr uint8_t bytes[] = {0x90, 0xE9};
#else
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
#endif
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
#if BLMAKE_ENHANCED
    constexpr auto signature = hat::compile_signature<
        " 49 8B 46 20"           // mov rax, [r14 + 0x20]
        " 48 8B 58 78"           // mov rbx, [rax + 0x78]
        " 48 85 DB"              // test rbx, rbx
        " 0F 84 ?? ?? ?? ??"     // jz done
        " F6 83 24 01 00 00 10"  // test byte [rbx + 0x124], 0x10
        " ?? ??"                 // jnz next <-- nop jmp next
        >();
    constexpr auto offset = 24;
#else
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
#endif
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
#if BLMAKE_ENHANCED
    constexpr auto signature = hat::compile_signature<
        " 49 8B F8"        // mov rdi, r8
        " 48 8B C2"        // mov rax, rdx
        " 48 8B F1"        // mov rsi, rcx
        " 49 83 78 68 00"  // cmp qword [r8 + 0x68], 0
        " ?? 0A"           // jnz parse <-- jmp parse
        " B8 01 00 00 00"  // mov eax, 1
        >();

    constexpr auto offset = 14;
    constexpr uint8_t bytes[] = {0xEB, 0x0A};
#else
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
#endif
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
        } else {
            purge_stale_defaults(cls);

            if (no_compress) {
                reinterpret_cast<UPackage*>(cls->Outer)->PackageFlags &= ~PKG_StoreCompressed;
            }

            // when recompiling a compiled package i.e., Core.u or WillowGame.u then some of our
            // prior patches actually cause problems (ironic) so if the class is not compiled but
            // has native functions bound to it, then we need to clear them first.
            if ((cls->ClassFlags & CLASS_Compiled) != 0) {
                cls->ClassFlags &= ~(CLASS_Parsed | CLASS_Compiled);

#if BLMAKE_ENHANCED
                // BL1E: Cooked packages retain the interface list so when compiling it checks to
                // see if its already been implemented and if so it errors with
                // "Interface ‘X’ is already implemented by ‘Y'"
                cls->Interfaces.count = 0;
#endif

                auto& funcs = cls->FuncMap.Pairs;
                for (int32_t i = 0; i < funcs.max_index(); ++i) {
                    if (funcs.is_allocated(i) && funcs.at(i).Value != nullptr) {
                        funcs.at(i).Value->iNative = 0;
                    }
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
#if BLMAKE_ENHANCED
    constexpr auto signature = hat::compile_signature<
        " 44 89 4C 24 20"           // mov [rsp + 0x20], r9d
        " 4C 89 44 24 18"           // mov [rsp + 0x18], r8
        " 48 89 54 24 10"           // mov [rsp + 0x10], rdx
        " 48 89 4C 24 08"           // mov [rsp + 0x8], rcx
        " 55 53 56 57"              // push rbp; push rbx; push rsi; push rdi
        " 48 8D 6C 24 D1"           // lea rbp, [rsp - 0x2f]
        " 48 81 EC 88 00 00 00"     // sub rsp, 0x88
        " 48 C7 45 E7 FE FF FF FF"  // mov qword [rbp - 0x19], -2
        " 49 8B F8"                 // mov rdi, r8
        " 48 8B C2"                 // mov rax, rdx
        " 48 8B F1"                 // mov rsi, rcx
        " 49 83 78 68 00"           // cmp qword [r8 + 0x68], 0
        >();
#else
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
#endif

    return install_hook(
        "ParseScripts",
        hat::find_pattern(signature, ".text"),
        reinterpret_cast<void*>(&parse_scripts_hook),
        reinterpret_cast<void**>(&fn_parse_scripts)
    );
}

bool hook_copy_inherited_components() {
#if BLMAKE_ENHANCED
    constexpr auto signature = hat::compile_signature<
        " 40 55 56 57"              // push rbp; push rsi; push rdi
        " 41 54 41 55 41 56 41 57"  // push r12; push r13; push r14; push r15
        " 48 8D AC 24 90 FE FF FF"  // lea rbp, [rsp - 0x170]
        " 48 81 EC 70 02 00 00"     // sub rsp, 0x270
        " 48 C7 45 18 FE FF FF FF"  // mov qword [rbp + 0x18], -2
        " 48 89 9C 24 C0 02 00 00"  // mov [rsp + 0x2c0], rbx
        " 48 8B 05 ?? ?? ?? ??"     // mov rax, [security_cookie]
        " 48 33 C4"                 // xor rax, rsp
        " 48 89 85 68 01 00 00"     // mov [rbp + 0x168], rax
        " 48 89 54 24 68"           // mov [rsp + 0x68], rdx
        " 4C 8B E1"                 // mov r12, rcx
        >();
#else
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
#endif

    return install_hook(
        "CopyInheritedComponents",
        hat::find_pattern(signature, ".text"),
        reinterpret_cast<void*>(&copy_inherited_components_hook),
        reinterpret_cast<void**>(&fn_copy_inherited_components)
    );
}

}  // namespace

bool install_make_commandlet_patches(bool no_compress_packages) {
    no_compress = no_compress_packages;
    return patch_superclass_check()
           && patch_parent_parsed_check()
           && patch_parse_scripts_early_out()
           && hook_parse_scripts()
           && hook_copy_inherited_components();
}

}  // namespace blmake
