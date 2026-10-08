#ifndef BLMAKE_STRUCTS_H
#define BLMAKE_STRUCTS_H

#include <cstddef>
#include <cstdint>
#include <utility>

namespace blmake {

using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

using std::int16_t;
using std::int32_t;
using std::int8_t;

using std::size_t;
using std::uintptr_t;

////////////////////////////////////////////////////////////////////////////////
// | CORE UNREAL TYPES |
////////////////////////////////////////////////////////////////////////////////

#pragma pack(push, 4)

struct UObject;
struct UClass;
struct UField;
struct UStruct;

using UProperty = void;

template <class T>
struct TArray {
    T* data;
    int32_t count;
    int32_t max;
};

struct FName {
    int index;
    int number;
};

struct UObject {
    uintptr_t* vftable;
    int32_t InternalIndex;
    uint64_t ObjectFlags;
    void* HashNext;
    void* HashOuterNext;
    void* StateFrame;
    UObject* Linker;
    void* LinkerIndex;
    int32_t NetIndex;
    UObject* Outer;
    FName Name;
    UClass* Class;
    UObject* ObjectArchetype;
};

struct UField : UObject {
    UStruct* SuperField;
    UField* Next;
};

struct UStruct {
    uint8_t _0[0x08];
    UField* Children;
    uint16_t PropertySize;
    uint8_t _1[0x1C + 0x02];
    UProperty* PropertyLink;
    uint8_t _2[0x10];
    TArray<UObject*> ScriptObjectReferences;
    uint8_t _3[0x04];
};

struct UClass : UStruct {
    uint8_t _0[0xC0];
    UObject* ClassDefaultObject;
    uint8_t _1[0x48];
    TArray<std::pair<void*, void*>> Interfaces;
};

#pragma pack(pop)

}  // namespace blmake

#endif  // BLMAKE_STRUCTS_H
