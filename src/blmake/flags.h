#ifndef BLMAKE_FLAGS_H
#define BLMAKE_FLAGS_H

#include "blcore/blcore.h"

namespace blmake {

// clang-format off
constexpr uint32_t CLASS_Compiled          = 0x00000002;
constexpr uint32_t CLASS_Parsed            = 0x00000010;
constexpr uint32_t PKG_StoreCompressed     = 0x02000000;

constexpr uint64_t RF_ClassDefaultObject   = 0x0000000000000200;
constexpr uint64_t RF_Public               = 0x0000000400000000;
constexpr uint64_t RF_Transient            = 0x0000400000000000;

constexpr uint32_t REN_ForceNoResetLoaders = 0x01;
// clang-format on

}  // namespace blmake

#endif  // BLMAKE_FLAGS_H
