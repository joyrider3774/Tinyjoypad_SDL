cmake_minimum_required(VERSION 3.14)
set(CMAKE_C_STANDARD 11)

# -----------------------------------------------------------------------------
# The Playdate port - a standalone CMake project in its own right, same shape
# as src/sdl3/ and src/sdl2/'s own CMakeLists.txt (see CLAUDE.md's "Directory
# layout / multi-port structure" section) but modeled on the official
# Playdate SDK's own CMake template (Examples/Hello World/CMakeLists.txt)
# instead, since that's the pattern every Playdate project - including the
# same-author sibling crisp-game-lib-portable-sdl project's own
# src/cglpPlaydate/CMakeLists.txt, which this file's own shape otherwise
# mirrors - is actually expected to use; the SDK's own playdate_game.cmake
# expects specific variable names (PLAYDATE_GAME_NAME/_DEVICE, SDK, TOOLCHAIN)
# this file sets up accordingly, not our own three-port convention.
#
# Two build outputs, matching every other Playdate project:
#  - Simulator (default, this repo's own primary target - "cd src/playdate &&
#    cmake -B build && cmake --build build" builds a pdex.dll straight into
#    Source/, loadable by PlaydateSimulator.exe): add_library(SHARED).
#  - Device (`-DTOOLCHAIN=armgcc`, needs the SDK's own arm-none-eabi-gcc
#    toolchain - see C:\playdate\arm-gnu-toolchain-*  on this machine):
#    add_executable(), cross-compiled to pdex.elf.
# Either way, playdate_game.cmake's own POST_BUILD step invokes `pdc` to pack
# Source/ into TinyJoypad.pdx right there in this directory - no extra step.
# -----------------------------------------------------------------------------

set(ENVSDK $ENV{PLAYDATE_SDK_PATH})

if (NOT ${ENVSDK} STREQUAL "")
	file(TO_CMAKE_PATH ${ENVSDK} SDK)
else()
	# No PLAYDATE_SDK_PATH set - fall back to this machine's own known SDK
	# install location (C:\playdate\PlaydateSDK) rather than the official
	# template's own ~/.Playdate/config lookup (a Mac/Linux convention this
	# Windows-only dev setup doesn't use).
	set(SDK "C:/playdate/PlaydateSDK")
endif()

if (NOT EXISTS ${SDK})
	message(FATAL_ERROR "SDK Path not found; set ENV value PLAYDATE_SDK_PATH")
	return()
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release")
set(CMAKE_XCODE_GENERATE_SCHEME TRUE)

set(PLAYDATE_GAME_NAME TinyJoypad)
set(PLAYDATE_GAME_DEVICE TinyJoypad_DEVICE)

project(${PLAYDATE_GAME_NAME} C ASM)

# Every shim/menu/games/*.c file is its own separate translation unit (see
# CLAUDE.md's "Translation-unit boundary" section) - globbed directly here
# alongside this port's own main.c, exactly the way src/sdl3/ and src/sdl2/'s
# own CMakeLists.txt already do. ../gameworld is untouched by this port -
# main.c writes a brand-new Playdate-native menu instead of reusing
# gameworld/menu.c's own BIOS-font one (see main.c's own header comment for
# why) but still links every one of its other files (all 33 games, both
# shims, menuGameList.c's addGames()) completely unmodified.
file(GLOB GameworldSources CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/../gameworld/*.c"
        "${PROJECT_SOURCE_DIR}/../gameworld/*.h"
        "${PROJECT_SOURCE_DIR}/../gameworld/games/*.c"
        "${PROJECT_SOURCE_DIR}/../gameworld/games/*.h"
)

if (TOOLCHAIN STREQUAL "armgcc")
	add_executable(${PLAYDATE_GAME_DEVICE} ${PROJECT_SOURCE_DIR}/main.c ${GameworldSources})
	target_include_directories(${PLAYDATE_GAME_DEVICE} PRIVATE ${PROJECT_SOURCE_DIR}/../gameworld)
else()
	add_library(${PLAYDATE_GAME_NAME} SHARED ${PROJECT_SOURCE_DIR}/main.c ${GameworldSources})
	target_include_directories(${PLAYDATE_GAME_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../gameworld)

	# playdate.cmake's own MSVC branch defines _WINDLL=1 automatically (main.c's
	# `#ifdef _WINDLL __declspec(dllexport) int eventHandler(...)` needs this to
	# export the symbol the Simulator loads by name) but its non-MSVC branch
	# does not - this repo's own toolchain is MinGW-w64/GCC throughout (see
	# every other port's own CMakeLists.txt), not MSVC like the sibling
	# cglpPlaydate project's own Windows build, so this needs doing by hand
	# here: __declspec(dllexport) is understood identically by MinGW's GCC,
	# it just never gets *defined* to trigger without this.
	if (MINGW)
		target_compile_definitions(${PLAYDATE_GAME_NAME} PRIVATE _WINDLL)
	endif()
endif ()

include(${SDK}/C_API/buildsupport/playdate_game.cmake)
