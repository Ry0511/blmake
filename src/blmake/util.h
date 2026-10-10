#ifndef BLMAKE_UTIL_H
#define BLMAKE_UTIL_H

#include <libhat.hpp>

#include <cstddef>
#include <cstdint>

#include "blmake/structs.h"

namespace blmake {

////////////////////////////////////////////////////////////////////////////////
// | PATCHING |
////////////////////////////////////////////////////////////////////////////////

bool write_code(std::byte* at, const uint8_t* bytes, size_t size);

bool apply_patch(
    const char* name,
    hat::scan_result result,
    size_t offset,
    const uint8_t* bytes,
    size_t size
);

bool install_hook(const char* name, hat::scan_result result, void* detour, void** original);

////////////////////////////////////////////////////////////////////////////////
// | ENGINE |
////////////////////////////////////////////////////////////////////////////////

using StaticFindObjectFn = UObject*(__cdecl*)(UClass * cls,
                                              UObject* outer,
                                              const wchar_t* name,
                                              uint32_t exact_class);

using RenameFn = uint32_t(__thiscall*)(
    UObject* self,
    const wchar_t* new_name,
    UObject* new_outer,
    uint32_t flags
);

hat::scan_result find_static_find_object();
hat::scan_result find_rename();
TArray<UObject*>* find_gobjects();

UObject* find_object(const wchar_t* path);

bool is_a(const UObject* obj, const UClass* cls);
bool is_inside(const UObject* obj, const UObject* outer);

}  // namespace blmake

#endif  // BLMAKE_UTIL_H
