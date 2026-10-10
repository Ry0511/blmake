#ifndef BLMAKE_MACROS_H
#define BLMAKE_MACROS_H

// NOLINTBEGIN(cppcoreguidelines-macro-usage, bugprone-macro-parentheses)
#define BLMAKE_DISALLOW_CREATE(Type)       \
    Type() = delete;                       \
    ~Type() = delete;                      \
    Type(const Type&) = delete;            \
    Type(Type&&) = delete;                 \
    Type& operator=(const Type&) = delete; \
    Type& operator=(Type&&) = delete
// NOLINTEND(cppcoreguidelines-macro-usage, bugprone-macro-parentheses)

#endif  // BLMAKE_MACROS_H
