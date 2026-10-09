#ifndef BLMAKE_TARRAY_H
#define BLMAKE_TARRAY_H

#include <cstdint>

namespace blmake {

#pragma pack(push, 4)

template <class T>
struct TArray {
    T* data;
    int32_t count;
    int32_t max;
};

#pragma pack(pop)

}  // namespace blmake

#endif  // BLMAKE_TARRAY_H
