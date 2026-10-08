cmake_minimum_required(VERSION 4.0)
project(
    blmake
    VERSION 1.0
    LANGUAGES CXX C
)

file(GLOB_RECURSE BLMAKE_SOURCE_LIST CONFIGURE_DEPENDS src/blmake/*.cpp src/blmake/*.h)

add_library(blmake SHARED ${BLMAKE_SOURCE_LIST})
