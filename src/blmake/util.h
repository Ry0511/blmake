#ifndef BLMAKE_UTIL_H
#define BLMAKE_UTIL_H

#include "blcore/util/hook.h"
#include "blcore/util/logging.h"

#include <string_view>

namespace blmake {

template <blcore::hook::FunctionPointer Fn>
bool hook(std::string_view name, void* target, Fn detour, Fn* original) {
    if (!blcore::hook::install(target, detour, original)) {
        return false;
    }
    BLCORE_INFO("Hooked {} at {}", name, target);
    return true;
}

}  // namespace blmake

#endif  // BLMAKE_UTIL_H
