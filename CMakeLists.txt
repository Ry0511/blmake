cmake_minimum_required(VERSION 4.0)
project(
    blmake
    VERSION 1.2
    LANGUAGES CXX C
)

include(FetchContent)
FetchContent_Declare(
    blcore
    GIT_REPOSITORY https://github.com/Ry0511/blcore.git
    GIT_TAG a1fe5e1d0c475ce4864ae894e37652996300fd9e
)
FetchContent_MakeAvailable(blcore)

file(GLOB_RECURSE BLMAKE_SOURCE_LIST CONFIGURE_DEPENDS src/blmake/*.cpp src/blmake/*.h)

add_library(blmake SHARED ${BLMAKE_SOURCE_LIST})
target_include_directories(blmake PRIVATE src)
target_link_libraries(blmake PRIVATE blcore::blcore minhook)

install(TARGETS blmake RUNTIME DESTINATION .)

if(MSVC)
    install(FILES $<TARGET_PDB_FILE:blmake> DESTINATION . OPTIONAL)
endif()
