#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <MinHook.h>
#include <libhat.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

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
using StaticFindObjectFn = UObject*(__cdecl*)(
    UClass* cls,
    UObject* outer,
    const wchar_t* name,
    uint32_t exact_class
);
using RenameFn = uint32_t(__thiscall*)(
    UObject* self,
    const wchar_t* new_name,
    UObject* new_outer,
    uint32_t flags
);
using DelegateImportTextFn = const wchar_t*(__thiscall*)(
    UProperty* self,
    const wchar_t* buffer,
    void* data,
    uint32_t port_flags,
    UObject* parent,
    void* error
);

DelegateImportTextFn fn_delegate_import_text = nullptr;

UObject* const ANY_PACKAGE = reinterpret_cast<UObject*>(-1);  // NOLINT(performance-no-int-to-ptr)

ParseScriptsFn fn_parse_scripts = nullptr;
CopyInheritedComponentsFn fn_copy_inherited_components = nullptr;
IsDebuggerPresentFn fn_is_debugger_present = nullptr;
StaticFindObjectFn fn_static_find_object = nullptr;
RenameFn fn_rename = nullptr;
TArray<UObject*>* gobjects = nullptr;

std::atomic patches_applied{false};
bool no_compress = false;

UClass* class_class = nullptr;
UClass* field_class = nullptr;
UClass* component_class = nullptr;
UClass* object_property_class = nullptr;
UClass* array_property_class = nullptr;
UClass* struct_property_class = nullptr;
int importing_delegate = 0;
std::vector<UObject*> purged_packages;
uint32_t stale_count = 0;

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

bool is_a(const UObject* obj, const UClass* cls) {
    for (const UStruct* it = obj->Class; it != nullptr; it = it->SuperField) {
        if (it == cls) {
            return true;
        }
    }
    return false;
}

// the Core classes the purge and the delegate fix need, looked up once
bool find_core_classes() {
    if (component_class != nullptr) {
        return true;
    }
    if (fn_static_find_object == nullptr) {
        return false;
    }
    const auto find_class = [](const wchar_t* path) {
        return reinterpret_cast<UClass*>(fn_static_find_object(nullptr, nullptr, path, 0));
    };
    class_class = find_class(L"Core.Class");
    field_class = find_class(L"Core.Field");
    object_property_class = find_class(L"Core.ObjectProperty");
    array_property_class = find_class(L"Core.ArrayProperty");
    struct_property_class = find_class(L"Core.StructProperty");
    UClass* component = find_class(L"Core.Component");
    if (class_class == nullptr || field_class == nullptr || object_property_class == nullptr
        || array_property_class == nullptr || struct_property_class == nullptr || component == nullptr) {
        BLMAKE_LOG("couldn't find the Core classes, stale subobjects are left in place");
        return false;
    }
    component_class = component;
    return true;
}

// the default object of a class in `package` that is being recompiled from source
bool is_recompiled_default(const UObject* obj, const UObject* package) {
    return (obj->ObjectFlags & RF_ClassDefaultObject) != 0 && obj->Outer == package
           && obj->Class->ScriptText != nullptr;
}

bool is_inside(const UObject* obj, const UObject* outer) {
    for (const UObject* it = obj->Outer; it != nullptr; it = it->Outer) {
        if (it == outer) {
            return true;
        }
    }
    return false;
}

int clear_refs_into(std::byte* data, const UStruct* scope, const UObject* target, bool own_only);

// nulls a property value's references to objects inside `target`
int clear_value(std::byte* value, const UProperty* prop, const UObject* target) {
    if (is_a(prop, object_property_class)) {
        auto* ref = reinterpret_cast<UObject**>(value);
        if (*ref != nullptr && is_inside(*ref, target)) {
            *ref = nullptr;
            return 1;
        }
        return 0;
    }
    if (is_a(prop, struct_property_class)) {
        return clear_refs_into(value, static_cast<const UStructProperty*>(prop)->Struct, target, false);
    }
    if (is_a(prop, array_property_class)) {
        const auto* array = reinterpret_cast<TArray<std::byte>*>(value);
        const UProperty* inner = static_cast<const UArrayProperty*>(prop)->Inner;
        int cleared = 0;
        for (int32_t i = 0; i < array->count; ++i) {
            cleared += clear_value(array->data + i * inner->ElementSize, inner, target);
        }
        return cleared;
    }
    return 0;
}

int clear_refs_into(std::byte* data, const UStruct* scope, const UObject* target, bool own_only) {
    int cleared = 0;
    for (const UProperty* prop = scope->PropertyLink; prop != nullptr; prop = prop->PropertyLinkNext) {
        if (own_only && prop->Outer != scope) {
            continue;
        }
        for (int32_t i = 0; i < prop->ArrayDim; ++i) {
            cleared += clear_value(data + prop->Offset + (i * prop->ElementSize), prop, target);
        }
    }
    return cleared;
}

void purge_stale_subobjects(UObject* package) {
    //
    // The cooked script packages are loaded before the make commandlet runs, so the default objects
    // of the classes we are about to recompile still own the subobjects they were cooked with. A
    // plain (non component) subobject collides with the template of the same name created from the
    // defaultproperties ("BEGIN OBJECT: name ... redefined") and, being inside the package, is saved
    // into the rebuilt package next to the new one. Component templates are reused in place by the
    // compiler so those stay; every plain subobject is renamed out of the way and, along with
    // everything below it, flagged transient so SavePackage skips it.
    //
    if (gobjects == nullptr || fn_rename == nullptr || !find_core_classes()) {
        return;
    }

    std::vector<UObject*> defaults;
    std::vector<UObject*> stale;
    std::vector<UObject*> roots;  // the outermost plain object of each stale group
    for (int32_t i = 0; i < gobjects->count; ++i) {
        UObject* obj = gobjects->data[i];
        if (obj == nullptr) {
            continue;
        }
        if (is_recompiled_default(obj, package)) {
            defaults.push_back(obj);
            continue;
        }

        bool plain_above = false;
        bool under_default = false;
        for (const UObject* outer = obj->Outer; outer != nullptr; outer = outer->Outer) {
            if (is_recompiled_default(outer, package)) {
                under_default = true;
                break;
            }
            plain_above = plain_above || !is_a(outer, component_class);
        }
        if (!under_default) {
            continue;
        }

        const bool plain = !is_a(obj, component_class);
        if (plain || plain_above) {
            stale.push_back(obj);
        }
        if (plain && !plain_above) {
            roots.push_back(obj);
        }
    }

    for (UObject* obj : stale) {
        obj->ObjectFlags |= RF_Transient;
    }
    for (UObject* obj : roots) {
        // a public object leaves a redirector with its old name behind when renamed
        obj->ObjectFlags &= ~RF_Public;
        const std::wstring name = L"BlmakeStale_" + std::to_wstring(stale_count++);
        fn_rename(obj, name.c_str(), nullptr, REN_ForceNoResetLoaders);
    }

    //
    // Only the inherited part of a recompiled default object is reinitialised (from the parent's)
    // before its defaultproperties are imported; the variables the class declares itself keep their
    // cooked values. Those still point at the class's own cooked components, which the compiler
    // rebuilds in place. Each "Begin Object" override is followed by a pass replacing references
    // to the parent template with the new component, and reaching a component through such a stale
    // pointer makes it replace the component's own archetype with itself. A freshly compiled class
    // has nothing in those variables until the defaultproperties set them, so clear them.
    //
    int cleared = 0;
    for (UObject* obj : defaults) {
        cleared += clear_refs_into(reinterpret_cast<std::byte*>(obj), obj->Class, obj, true);
    }

    BLMAKE_LOG(
        "package at {}: renamed {} stale subobject(s), {} flagged transient, {} stale reference(s) cleared",
        static_cast<void*>(package),
        roots.size(),
        stale.size(),
        cleared
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
            // the first class of a package seen here means all of its sources are loaded
            if (std::ranges::find(purged_packages, cls->Outer) == purged_packages.end()) {
                purged_packages.push_back(cls->Outer);
                purge_stale_subobjects(cls->Outer);
            }

            if (no_compress) {
                reinterpret_cast<UPackage*>(cls->Outer)->PackageFlags &= ~PKG_StoreCompressed;
            }

            // when recompiling a compiled package i.e., Core.u or WillowGame.u then some of our
            // prior patches actually cause problems (ironic) so if the class is not compiled but
            // has native functions bound to it, then we need to clear them first.
            if ((cls->ClassFlags & CLASS_Compiled) != 0) {
                cls->ClassFlags &= ~(CLASS_Parsed | CLASS_Compiled);
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

// const TCHAR* UDelegateProperty::ImportText(...) is thiscall; a fastcall's unused edx stands in
const wchar_t* __fastcall delegate_import_text_hook(
    UProperty* self,
    void* /*edx*/,
    const wchar_t* buffer,
    void* data,
    uint32_t port_flags,
    UObject* parent,
    void* error
) {
    ++importing_delegate;
    const wchar_t* result = fn_delegate_import_text(self, buffer, data, port_flags, parent, error);
    --importing_delegate;
    return result;
}

// UObject::StaticFindObject
UObject* __cdecl static_find_object_hook(
    UClass* cls,
    UObject* outer,
    const wchar_t* name,
    uint32_t exact_class
) {
    UObject* found = fn_static_find_object(cls, outer, name, exact_class);

    //
    // A delegate in the defaultproperties is imported as "<Class>.<Function>" (the compiler adds
    // the class) and the class is looked up by name in any package. With the cooked script
    // packages loaded, the first object with that name can be a local or parameter of some
    // function (Interaction -> a WillowHUD function's Interaction local), so the function isn't
    // found and the assignment fails. A field can never be what a delegate is bound to, so when
    // the lookup lands on one, look for a class of that name instead.
    //
    if (importing_delegate > 0 && outer == ANY_PACKAGE && found != nullptr && find_core_classes()
        && is_a(found, field_class) && !is_a(found, class_class)) {
        UObject* as_class = fn_static_find_object(class_class, outer, name, exact_class);
        if (as_class != nullptr) {
            return as_class;
        }
    }
    return found;
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

////////////////////////////////////////////////////////////////////////////////
// | ENGINE FUNCTIONS |
////////////////////////////////////////////////////////////////////////////////

bool find_gobjects() {
    // UObject::GObjObjects, same signature as unrealsdk
    constexpr auto signature = hat::compile_signature<
        " 8B 0D ?? ?? ?? ??"  // mov ecx, [GObjObjects]
        " 8B 04 ??"           // mov eax, [ecx + esi*4]
        " 8B 50 ??"           // mov edx, [eax + 0xc]
        " 21 58 ??"           // and [eax + 8], ebx
        " 89 50 ??"           // mov [eax + 0xc], edx
        >();

    const auto result = hat::find_pattern(signature, ".text");
    if (!result.has_result()) {
        return false;
    }
    std::memcpy(&gobjects, result.get() + 2, sizeof gobjects);
    BLMAKE_LOG("found GObjects at {}", static_cast<void*>(gobjects));
    return true;
}

bool hook_static_find_object() {
    // UObject::StaticFindObject, same signature as unrealsdk
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

    // the trampoline is what blmake itself calls
    return install_hook(
        "StaticFindObject",
        hat::find_pattern(signature, ".text"),
        reinterpret_cast<void*>(&static_find_object_hook),
        reinterpret_cast<void**>(&fn_static_find_object)
    );
}

bool hook_delegate_import_text() {
    constexpr auto signature = hat::compile_signature<
        " B8 20 10 00 00"          // mov eax, 0x1020
        " E8 ?? ?? ?? ??"          // call _alloca_probe
        " A1 ?? ?? ?? ??"          // mov eax, [security_cookie]
        " 33 C4"                   // xor eax, esp
        " 89 84 24 1C 10 00 00"    // mov [esp + 0x101c], eax
        " 8B 84 24 28 10 00 00"    // mov eax, [esp + 0x1028]
        " 53"                      // push ebx
        " 89 44 24 1C"             // mov [esp + 0x1c], eax
        " 8B 84 24 38 10 00 00"    // mov eax, [esp + 0x1038]
        " 8B D9"                   // mov ebx, ecx
        " 8B 8C 24 30 10 00 00"    // mov ecx, [esp + 0x1030]
        " 50"                      // push eax
        " 51"                      // push ecx
        " 8B CB"                   // mov ecx, ebx
        " E8 ?? ?? ?? ??"          // call UProperty::ValidateImportFlags
        " 85 C0"                   // test eax, eax
        " 75"                      // jnz
        >();

    return install_hook(
        "UDelegateProperty::ImportText",
        hat::find_pattern(signature, ".text"),
        reinterpret_cast<void*>(&delegate_import_text_hook),
        reinterpret_cast<void**>(&fn_delegate_import_text)
    );
}

bool find_rename() {
    // UBOOL UObject::Rename(const TCHAR* NewName, UObject* NewOuter, ERenameFlags Flags)
    constexpr auto signature = hat::compile_signature<
        " 6A FF"               // push -1
        " 68 ?? ?? ?? ??"      // push handler
        " 64 A1 00 00 00 00"   // mov eax, fs:[0]
        " 50"                  // push eax
        " 83 EC 2C"            // sub esp, 0x2c
        " 53"                  // push ebx
        " 55"                  // push ebp
        " 56"                  // push esi
        " 57"                  // push edi
        " A1 ?? ?? ?? ??"      // mov eax, [security_cookie]
        " 33 C4"               // xor eax, esp
        " 50"                  // push eax
        " 8D 44 24 40"         // lea eax, [esp + 0x40]
        " 64 A3 00 00 00 00"   // mov fs:[0], eax
        " 8B F1"               // mov esi, ecx
        " 8B 6C 24 54"         // mov ebp, [esp + 0x54] (NewOuter)
        " C7 44 24 14 00 00 00 00"  // mov [esp + 0x14], 0
        " 85 ED"               // test ebp, ebp
        " 0F 84 ?? ?? ?? ??"   // jz no_outer_check
        " 8B 4E 34"            // mov ecx, [esi + 0x34] (Class)
        >();

    const auto result = hat::find_pattern(signature, ".text");
    if (!result.has_result()) {
        return false;
    }
    fn_rename = reinterpret_cast<RenameFn>(result.get());
    BLMAKE_LOG("found Rename at {}", static_cast<void*>(result.get()));
    return true;
}

bool try_install() {
    const bool installed = patch_superclass_check()
                           && patch_parent_parsed_check()
                           && patch_parse_scripts_early_out()
                           && hook_parse_scripts()
                           && hook_copy_inherited_components();

    // optional: without these the stale default subobjects are left where they are
    if (installed && !(find_gobjects() && hook_static_find_object() && find_rename())) {
        BLMAKE_LOG("couldn't find the functions needed to purge stale default subobjects");
    }
    // optional: without it a delegate default can resolve its class to a cooked local
    if (installed && fn_static_find_object != nullptr && !hook_delegate_import_text()) {
        BLMAKE_LOG("couldn't hook UDelegateProperty::ImportText");
    }
    return installed;
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
