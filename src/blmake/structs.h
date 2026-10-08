#ifndef BLMAKE_STRUCTS_H
#define BLMAKE_STRUCTS_H

#include <cstddef>
#include <cstdint>

namespace blmake {

using std::int32_t;
using std::size_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;
using std::uintptr_t;

////////////////////////////////////////////////////////////////////////////////
// | CORE UNREAL TYPES |
////////////////////////////////////////////////////////////////////////////////

#pragma pack(push, 4)

struct UObject;
struct UField;
struct UStruct;
struct UClass;
struct UProperty;
struct UTextBuffer;

template <class T>
struct TArray {
    T* data;
    int32_t count;
    int32_t max;
};

struct FName {
    int32_t index;
    int32_t number;
};

struct FImplementedInterface {
    UClass* Class;
    UProperty* PointerProperty;
};

constexpr uint32_t CLASS_Compiled = 0x00000002;
constexpr uint32_t CLASS_Parsed = 0x00000010;

struct UObject {
    uintptr_t* vftable;
    int32_t InternalIndex;
    uint64_t ObjectFlags;
    UObject* HashNext;
    UObject* HashOuterNext;
    void* StateFrame;
    void* Linker;
    int32_t LinkerIndex;
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

struct UStruct : UField {
    UTextBuffer* ScriptText;
    UTextBuffer* CppText;
    UField* Children;
    int32_t PropertiesSize;
    TArray<uint8_t> Script;
    int32_t TextPos;
    int32_t Line;
    uint8_t _0[0x2C];
};

struct UClass : UStruct {
    uint8_t _0[0x54];
    uint32_t ClassFlags;
    uint8_t _1[0x68];
    UObject* ClassDefaultObject;
    uint8_t _2[0x48];
    TArray<FImplementedInterface> Interfaces;
};

#pragma pack(pop)

}  // namespace blmake

#endif  // BLMAKE_STRUCTS_H
