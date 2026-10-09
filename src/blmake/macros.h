#ifndef BLMAKE_MACROS_H
#define BLMAKE_MACROS_H

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define BLMAKE_DISALLOW_CREATE(Type)       \
    Type() = delete;                       \
    Type(const Type&) = delete;            \
    Type(Type&&) = delete;                 \
    Type& operator=(const Type&) = delete; \
    Type& operator=(Type&&) = delete

#endif  // BLMAKE_MACROS_H
