#ifndef BLMAKE_UTIL_H
#define BLMAKE_UTIL_H

#include <libhat.hpp>

namespace blmake {

bool write_code(std::byte* at, const uint8_t* bytes, size_t size);

bool apply_patch(
    const char* name,
    hat::scan_result result,
    size_t offset,
    const uint8_t* bytes,
    size_t size
);

bool install_hook(const char* name, hat::scan_result result, void* detour, void** original);

}

#endif  // BLMAKE_UTIL_H
