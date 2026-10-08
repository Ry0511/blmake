cmake_minimum_required(VERSION 4.0)
project(
    blmake
    VERSION 1.0
    LANGUAGES CXX C
)

################################################################################
# | MIN HOOK |
################################################################################

include(FetchContent)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(minhook)

FetchContent_Declare(
    libhat
    GIT_REPOSITORY https://github.com/BasedInc/libhat.git
    GIT_TAG v0.11.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libhat)

################################################################################
# | BLMAKE |
################################################################################

file(GLOB_RECURSE BLMAKE_SOURCE_LIST CONFIGURE_DEPENDS src/blmake/*.cpp src/blmake/*.h)

add_library(blmake SHARED ${BLMAKE_SOURCE_LIST})
target_link_libraries(blmake PRIVATE minhook libhat::libhat)
