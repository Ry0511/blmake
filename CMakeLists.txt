cmake_minimum_required(VERSION 4.0)
project(
    blmake
    VERSION 1.1
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
    EXCLUDE_FROM_ALL
)
set(CMAKE_SKIP_INSTALL_RULES ON)
FetchContent_MakeAvailable(minhook)
set(CMAKE_SKIP_INSTALL_RULES OFF)

set(LIBHAT_INSTALL_TARGET OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libhat
    GIT_REPOSITORY https://github.com/BasedInc/libhat.git
    GIT_TAG v0.11.1
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(libhat)

################################################################################
# | BLMAKE |
################################################################################

file(GLOB_RECURSE BLMAKE_SOURCE_LIST CONFIGURE_DEPENDS src/blmake/*.cpp src/blmake/*.h)

add_library(blmake SHARED ${BLMAKE_SOURCE_LIST})
target_include_directories(blmake PRIVATE src)
target_link_libraries(blmake PRIVATE minhook libhat::libhat)

# clean before install - only do this when installing into out/install
install(CODE "
    if(CMAKE_INSTALL_PREFIX MATCHES \"/out/install/\")
        file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}\")
    endif()
")

install(TARGETS blmake RUNTIME DESTINATION .)

if(MSVC)
    install(FILES $<TARGET_PDB_FILE:blmake> DESTINATION . OPTIONAL)
endif()
