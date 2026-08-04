cmake_minimum_required(VERSION 3.5)

project(TinyjoypadSDL3 LANGUAGES C)

# -----------------------------------------------------------------------------
# The SDL3 port - a standalone CMake project in its own right, not a
# subdirectory of a repo-wide top-level CMakeLists.txt (there isn't one; see
# CLAUDE.md's "Directory layout / multi-port structure" section for why -
# mirrors the same-author sibling crisp-game-lib-portable-sdl project's own
# src/cglpSDL3/, src/cglpSDL2/, src/cglpPlaydate/, ... layout exactly, one
# fully self-contained CMakeLists.txt per port, each `cd`'d into and built on
# its own). Build from here:
#
#   cd src/sdl3
#   cmake -B build -G Ninja -DUSE_VENDORED_SDL=1
#   cmake --build build
#
# Pulls in the shared, port-agnostic "game world" from ../gameworld (see that
# directory's own machineDependent.h for the interface boundary every port
# implements) - a future src/sdl2/ or src/playdate/ port would reuse that
# same directory unchanged, adding only its own platform-specific files
# alongside its own CMakeLists.txt at that same src/<port>/ level.
# -----------------------------------------------------------------------------

# Create an option to switch between a system sdl library and a vendored sdl library
option(USE_VENDORED_SDL "Use vendored libraries" OFF)

# The vendored SDL3 checkout lives at the repo root (${PROJECT_SOURCE_DIR}/../../SDL3),
# not inside this port directory - shared vendoring location, same as the
# sibling cglpSDL3 project's own SDL3/ (one checkout, referenced by relative
# path from whichever port needs it, rather than duplicated per-port).
if(USE_VENDORED_SDL)
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	set(SDL_STATIC_PIC ON CACHE BOOL "" FORCE)
	add_subdirectory("${PROJECT_SOURCE_DIR}/../../SDL3" "${PROJECT_SOURCE_DIR}/build/SDL3" EXCLUDE_FROM_ALL)
else()
    # 1. Look for a SDL3 package, 2. look for the SDL3 component and 3. fail if none can be found
    find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)
endif()


# Every shim/menu/games/*.c file is its own separate translation unit (see
# CLAUDE.md's "Translation-unit boundary" section) - gamesMain.c doesn't
# #include any of their .c bodies, so ../gameworld/*.c and
# ../gameworld/games/*.c are globbed directly here alongside this port's own
# *.c, each compiled and linked independently like any normal multi-file C
# project. ../gameworld is the ENTIRE reason this is a multi-port-ready
# layout - it never #includes SDL.h and knows nothing about this port.
file(GLOB SRC CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/*.c"
        "${PROJECT_SOURCE_DIR}/*.h"
        "${PROJECT_SOURCE_DIR}/../gameworld/*.c"
        "${PROJECT_SOURCE_DIR}/../gameworld/*.h"
        "${PROJECT_SOURCE_DIR}/../gameworld/games/*.c"
        "${PROJECT_SOURCE_DIR}/../gameworld/games/*.h"
)

if (WIN32)
	set(CMAKE_EXE_LINKER_FLAGS "-static -mwindows")
endif()

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE ${SRC} )

target_include_directories(${PROJECT_NAME} PRIVATE
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/../gameworld"
)

if(USE_VENDORED_SDL)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL3::SDL3-static
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL3::SDL3
    )
endif()

# md_drawGameThumbnail() (sdlBackend.c) resolves thumbnails relative to
# SDL_GetBasePath() - the built exe's own directory, not the launching
# shell's cwd - so the repo-root assets/ tree needs to be mirrored alongside
# it after every build for thumbnails (or any future asset) to be found,
# both during development and in a packaged/distributed copy. assets/ lives
# at the repo root (${PROJECT_SOURCE_DIR}/../../assets), not inside this
# port directory, since the actual asset files (thumbnails) are themselves
# port-agnostic content, same reasoning as ../gameworld.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/../../assets"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
)
