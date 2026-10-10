#include <libhat.hpp>

#include "blmake/logging.h"
#include "blmake/patches/delegate_import.h"
#include "blmake/structs.h"
#include "blmake/util.h"

namespace blmake {

namespace {

using DelegateImportTextFn = const wchar_t*(__thiscall*)(UProperty * self,
                                                         const wchar_t* buffer,
                                                         void* data,
                                                         uint32_t port_flags,
                                                         UObject* parent,
                                                         void* error);

DelegateImportTextFn fn_delegate_import_text = nullptr;
StaticFindObjectFn fn_static_find_object = nullptr;

const auto ANY_PACKAGE = reinterpret_cast<UObject*>(-1);

int importing_delegate = 0;
UClass* cls_field = nullptr;

bool find_field_class() {
    if (cls_field == nullptr) {
        cls_field = reinterpret_cast<UClass*>(
            fn_static_find_object(nullptr, nullptr, L"Core.Field", 0)
        );
    }

    if (cls_field == nullptr) {
        BLMAKE_LOG("find_field_class could not find Field class - the engine is probably still initialising");
    }

    return cls_field != nullptr;
}

// const TCHAR* UDelegateProperty::ImportText
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

UObject* __cdecl static_find_object_hook(
    UClass* cls,
    UObject* outer,
    const wchar_t* name,
    uint32_t exact_class
) {
    UObject* found = fn_static_find_object(cls, outer, name, exact_class);

    if (
        importing_delegate <= 0
        || outer != ANY_PACKAGE
        || found == nullptr
        || !find_field_class()
    ) {
        return found;
    }

    // absolutely class innit
    UClass* class_class = found->Class->Class;
    if (is_a(found, cls_field) && !is_a(found, class_class)) {
        UObject* as_class = fn_static_find_object(class_class, outer, name, exact_class);
        if (as_class != nullptr) {
            return as_class;
        }
    }

    return found;
}

bool hook_static_find_object() {
    return install_hook(
        "StaticFindObject",
        find_static_find_object(),
        reinterpret_cast<void*>(&static_find_object_hook),
        reinterpret_cast<void**>(&fn_static_find_object)
    );
}

bool hook_delegate_import_text() {
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

    return install_hook(
        "UDelegateProperty::ImportText",
        hat::find_pattern(signature, ".text"),
        reinterpret_cast<void*>(&delegate_import_text_hook),
        reinterpret_cast<void**>(&fn_delegate_import_text)
    );
}

}  // namespace

bool install_delegate_import_fix() {
    return hook_static_find_object() && hook_delegate_import_text();
}

}  // namespace blmake
