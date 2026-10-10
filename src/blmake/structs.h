#ifndef BLMAKE_STRUCTS_H
#define BLMAKE_STRUCTS_H

#include <cstddef>
#include <cstdint>

#include "macros.h"
#include "tarray.h"
#include "tmap.h"

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

struct FName {
    int32_t index;
    int32_t number;
};

struct FImplementedInterface {
    UClass* Class;
    UProperty* PointerProperty;
};

// clang-format off
constexpr uint32_t CLASS_Compiled          = 0x00000002;
constexpr uint32_t CLASS_Parsed            = 0x00000010;
constexpr uint32_t PKG_StoreCompressed     = 0x02000000;

constexpr uint64_t RF_ClassDefaultObject   = 0x0000000000000200;
constexpr uint64_t RF_Public               = 0x0000000400000000;
constexpr uint64_t RF_Transient            = 0x0000400000000000;

constexpr uint32_t REN_ForceNoResetLoaders = 0x01;
// clang-format on

struct UObject {
    BLMAKE_DISALLOW_CREATE(UObject);

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

struct UPackage : UObject {
    BLMAKE_DISALLOW_CREATE(UPackage);

    uint8_t _0[0x60];
    uint32_t PackageFlags;
};

struct UField : UObject {
    BLMAKE_DISALLOW_CREATE(UField);

    UStruct* SuperField;
    UField* Next;
};

struct UStruct : UField {
    BLMAKE_DISALLOW_CREATE(UStruct);

    UTextBuffer* ScriptText;
    UTextBuffer* CppText;
    UField* Children;
    int32_t PropertiesSize;
    TArray<uint8_t> Script;
    int32_t TextPos;
    int32_t Line;
    uint8_t _0[0x08];
    UProperty* PropertyLink;
    uint8_t _1[0x20];
};

struct UProperty : UField {
    BLMAKE_DISALLOW_CREATE(UProperty);

    int32_t ArrayDim;
    int32_t ElementSize;
    uint32_t PropertyFlags;
    uint8_t _0[0x14];
    int32_t Offset;
    UProperty* PropertyLinkNext;
    uint8_t _1[0x20];
};

struct UObjectProperty : UProperty {
    BLMAKE_DISALLOW_CREATE(UObjectProperty);

    UClass* PropertyClass;
};

struct UArrayProperty : UProperty {
    BLMAKE_DISALLOW_CREATE(UArrayProperty);

    UProperty* Inner;
};

struct UStructProperty : UProperty {
    BLMAKE_DISALLOW_CREATE(UStructProperty);

    UStruct* Struct;
};

struct UFunction : UStruct {
    BLMAKE_DISALLOW_CREATE(UFunction);

    uint32_t FunctionFlags;
    uint16_t iNative;
};

struct UClass : UStruct {
    BLMAKE_DISALLOW_CREATE(UClass);

    uint8_t _0[0x18];
    TMap<FName, UFunction*> FuncMap;
    uint32_t ClassFlags;
    uint8_t _1[0x68];
    UObject* ClassDefaultObject;
    uint8_t _2[0x0C];
    TMap<FName, UObject*> ComponentNameToDefaultObjectMap;
    TArray<FImplementedInterface> Interfaces;
};

#pragma pack(pop)

}  // namespace blmake

#endif  // BLMAKE_STRUCTS_H
