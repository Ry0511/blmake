#include "blcore/engine/engine.h"
#include "blcore/util/sigscan.h"

#include <libhat.hpp>

#include "blcore/ue3/uobject/uclass.h"
#include "blcore/ue3/uobject/uobject.h"
#include "blcore/ue3/uprop/uproperty.h"
#include "blcore/util/logging.h"
#include "blmake/patches/delegate_import.h"
#include "blmake/util.h"

namespace blmake {

namespace engine = blcore::engine;
namespace ue3 = blcore::ue3;

namespace {

using StaticFindObjectFn = ue3::UObject*(__cdecl*)(ue3::UClass * cls,
                                                   ue3::UObject* outer,
                                                   const wchar_t* name,
                                                   uint32_t exact_class);

using DelegateImportTextFn = const wchar_t*(__thiscall*)(ue3::UProperty * self,
                                                         const wchar_t* buffer,
                                                         void* data,
                                                         uint32_t port_flags,
                                                         ue3::UObject* parent,
                                                         void* error);

DelegateImportTextFn fn_delegate_import_text = nullptr;
StaticFindObjectFn fn_static_find_object = nullptr;

int importing_delegate = 0;
ue3::UClass* cls_field = nullptr;

bool find_field_class() {
    if (cls_field == nullptr) {
        cls_field = reinterpret_cast<ue3::UClass*>(
            fn_static_find_object(nullptr, nullptr, L"Core.Field", 0)
        );
    }

    if (cls_field == nullptr) {
        BLCORE_ERROR("could not find Core.Field");
    }

    return cls_field != nullptr;
}

// const TCHAR* UDelegateProperty::ImportText
const wchar_t* __fastcall delegate_import_text_hook(
    ue3::UProperty* self,
#ifndef WILLOW64
    void* /*edx*/,
#endif
    const wchar_t* buffer,
    void* data,
    uint32_t port_flags,
    ue3::UObject* parent,
    void* error
) {
    ++importing_delegate;
    const wchar_t* result = fn_delegate_import_text(self, buffer, data, port_flags, parent, error);
    --importing_delegate;
    return result;
}

ue3::UObject* __cdecl static_find_object_hook(
    ue3::UClass* cls,
    ue3::UObject* outer,
    const wchar_t* name,
    uint32_t exact_class
) {
    ue3::UObject* found = fn_static_find_object(cls, outer, name, exact_class);

    if (
        importing_delegate <= 0
        || outer != engine::ANY_PACKAGE
        || found == nullptr
        || !find_field_class()
    ) {
        return found;
    }

    // absolutely class innit
    ue3::UClass* class_class = found->Class()->Class();
    if (engine::is_instance(found, cls_field) && !engine::is_instance(found, class_class)) {
        ue3::UObject* as_class = fn_static_find_object(class_class, outer, name, exact_class);
        if (as_class != nullptr) {
            return as_class;
        }
    }

    return found;
}

void* find_delegate_import_text() {
#ifdef WILLOW64
    constexpr auto signature = hat::compile_signature<
        " 40 55 56 57"                 // push rbp; push rsi; push rdi
        " 41 54 41 55 41 56 41 57"     // push r12; push r13; push r14; push r15
        " 48 8D AC 24 80 F0 FF FF"     // lea rbp, [rsp - 0xf80]
        " B8 80 10 00 00"              // mov eax, 0x1080
        " E8 ?? ?? ?? ??"              // call _alloca_probe
        " 48 2B E0"                    // sub rsp, rax
        " 48 C7 44 24 68 FE FF FF FF"  // mov qword [rsp + 0x68], -2
        " 48 89 9C 24 C8 10 00 00"     // mov [rsp + 0x10c8], rbx
        " 48 8B 05 ?? ?? ?? ??"        // mov rax, [security_cookie]
        " 48 33 C4"                    // xor rax, rsp
        " 48 89 85 70 0F 00 00"        // mov [rbp + 0xf70], rax
        " 4C 89 44 24 60"              // mov [rsp + 0x60], r8
        " 4C 8B E2"                    // mov r12, rdx
        " 48 8B F9"                    // mov rdi, rcx
        " 4C 8B 85 E8 0F 00 00"        // mov r8, [rbp + 0xfe8]
        " 41 8B D1"                    // mov edx, r9d
        " E8 ?? ?? ?? ??"              // call UProperty::ValidateImportFlags
        " 85 C0"                       // test eax, eax
        " 75"                          // jnz
        >();
#else
    constexpr auto signature = hat::compile_signature<
        " B8 20 10 00 00"        // mov eax, 0x1020
        " E8 ?? ?? ?? ??"        // call _alloca_probe
        " A1 ?? ?? ?? ??"        // mov eax, [security_cookie]
        " 33 C4"                 // xor eax, esp
        " 89 84 24 1C 10 00 00"  // mov [esp + 0x101c], eax
        " 8B 84 24 28 10 00 00"  // mov eax, [esp + 0x1028]
        " 53"                    // push ebx
        " 89 44 24 1C"           // mov [esp + 0x1c], eax
        " 8B 84 24 38 10 00 00"  // mov eax, [esp + 0x1038]
        " 8B D9"                 // mov ebx, ecx
        " 8B 8C 24 30 10 00 00"  // mov ecx, [esp + 0x1030]
        " 50"                    // push eax
        " 51"                    // push ecx
        " 8B CB"                 // mov ecx, ebx
        " E8 ?? ?? ?? ??"        // call UProperty::ValidateImportFlags
        " 85 C0"                 // test eax, eax
        " 75"                    // jnz
        >();
#endif

    return blcore::sigscan::find("UDelegateProperty::ImportText", signature);
}

}  // namespace

bool install_delegate_import_fix() {
    void* static_find_object = engine::static_find_object_address();
    void* delegate_import_text = find_delegate_import_text();
    if (static_find_object == nullptr || delegate_import_text == nullptr) {
        return false;
    }

    return hook("StaticFindObject", static_find_object, &static_find_object_hook, &fn_static_find_object)
           && hook(
               "UDelegateProperty::ImportText",
               delegate_import_text,
               reinterpret_cast<DelegateImportTextFn>(&delegate_import_text_hook),
               &fn_delegate_import_text
           );
}

}  // namespace blmake
