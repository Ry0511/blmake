#ifndef BLMAKE_TMAP_H
#define BLMAKE_TMAP_H

#include <bitset>
#include <cstdint>

#include "macros.h"
#include "tarray.h"

namespace blmake {

#pragma pack(push, 4)

////////////////////////////////////////////////////////////////////////////////
// | BIT ARRAY |
////////////////////////////////////////////////////////////////////////////////

class TBitArray {
   public:
    BLMAKE_DISALLOW_CREATE(TBitArray);

    bool at(int32_t index) {
        return bits()[index / 32].test(index % 32);
    }

    void clear(int32_t index) {
        bits()[index / 32].reset(index % 32);
    }

   private:
    using Bits = std::bitset<32>;

    Bits* bits() {
        return m_SecondaryData != nullptr ? m_SecondaryData : m_InlineData;
    }

    Bits m_InlineData[4];
    Bits* m_SecondaryData;
    int32_t m_NumBits;
    int32_t m_MaxBits;
};

////////////////////////////////////////////////////////////////////////////////
// | SPARSE ARRAY |
////////////////////////////////////////////////////////////////////////////////

template <class T>
class TSparseArray {
   public:
    BLMAKE_DISALLOW_CREATE(TSparseArray);

    int32_t max_index() {
        return m_Data.count;
    }

    bool is_allocated(int32_t index) {
        return m_AllocationFlags.at(index);
    }

    T& at(int32_t index) {
        return m_Data.data[index].ElementData;
    }

    void remove(int32_t index) {
        m_Data.data[index].NextFreeIndex = m_NumFreeIndices > 0 ? m_FirstFreeIndex : -1;
        m_FirstFreeIndex = index;
        ++m_NumFreeIndices;
        m_AllocationFlags.clear(index);
    }

   private:
    union TSparseArrayElementOrFreeListLink {
        T ElementData;
        int32_t NextFreeIndex;
    };

    TArray<TSparseArrayElementOrFreeListLink> m_Data;
    TBitArray m_AllocationFlags;
    int32_t m_FirstFreeIndex;
    int32_t m_NumFreeIndices;
};

////////////////////////////////////////////////////////////////////////////////
// | TSet |
////////////////////////////////////////////////////////////////////////////////

template <class T>
class TSet {
   public:
    BLMAKE_DISALLOW_CREATE(TSet);

    int32_t max_index() {
        return m_Elements.max_index();
    }

    bool is_allocated(int32_t index) {
        return m_Elements.is_allocated(index);
    }

    T& at(int32_t index) {
        return m_Elements.at(index).Value;
    }

    void remove(int32_t index) {
        const TSetElement& removed = m_Elements.at(index);
        if (m_HashSize > 0) {
            int32_t* link = &hash()[removed.HashIndex & (m_HashSize - 1)];

            while (*link != -1 && *link != index) {
                link = &m_Elements.at(*link).HashNextId;
            }

            if (*link == index) {
                *link = removed.HashNextId;
            }
        }
        m_Elements.remove(index);
    }

   private:
    struct TSetElement {
        T Value;
        int32_t HashNextId;
        int32_t HashIndex;
    };

    int32_t* hash() {
        return m_HashSecondary != nullptr ? m_HashSecondary : &m_HashInline;
    }

    TSparseArray<TSetElement> m_Elements;
    int32_t m_HashInline;
    int32_t* m_HashSecondary;
    int32_t m_HashSize;
};

////////////////////////////////////////////////////////////////////////////////
// | TMap |
////////////////////////////////////////////////////////////////////////////////

template <class K, class V>
struct TMap {
    BLMAKE_DISALLOW_CREATE(TMap);

    struct TPair {
        K Key;
        V Value;
    };

    TSet<TPair> Pairs;
};

#pragma pack(pop)

}  // namespace blmake

#endif  // BLMAKE_TMAP_H
