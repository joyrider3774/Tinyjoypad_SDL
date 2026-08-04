#ifndef TINYJOYPAD_SDL2_H
#define TINYJOYPAD_SDL2_H

// Keybind mapping consumed by CInput.c (copied near-verbatim from
// src/sdl3/tinyjoypadSDL3.h, itself copied near-verbatim from the sibling
// crisp-game-lib-portable-sdl project's own cglpSDL2.h/cglpSDL3.h) -
// BUTTON_A/BUTTON_B map to TinyJoypad's own Fire/Fire2, BUTTON_MENU to
// Start (used for the quit-confirmation dialog, see main.c).
// BUTTON_DARKSWITCH is kept defined only because CInput.c's own keyboard-
// event switch references it - TinyJoypad's games don't use dark-color-
// mode, so nothing ever acts on it.
//
// Non-letter SDLK_* names (UP/DOWN/LEFT/RIGHT/ESCAPE/PAGEUP/PAGEDOWN) are
// identical between SDL2 and SDL3 - only the plain letter keys differ:
// SDL2's own SDLK_x/SDLK_c/SDLK_s/SDLK_g/SDLK_d are lowercase (matching
// their ASCII values, SDL2's original convention), while SDL3 renamed
// every letter keycode to uppercase (SDLK_X/etc, matching the physical key
// label instead) - confirmed the hard way, this port's first build failed
// on exactly these 5 macros with "undeclared identifier 'SDLK_X'".
#define BUTTON_UP SDLK_UP
#define BUTTON_DOWN SDLK_DOWN
#define BUTTON_LEFT SDLK_LEFT
#define BUTTON_RIGHT SDLK_RIGHT
#define BUTTON_A SDLK_x
#define BUTTON_B SDLK_c
#define BUTTON_MENU SDLK_ESCAPE
#define BUTTON_VOLDOWN SDLK_PAGEDOWN
#define BUTTON_VOLUP SDLK_PAGEUP
#define BUTTON_SOUNDSWITCH SDLK_s
#define BUTTON_GLOWSWITCH SDLK_g
#define BUTTON_DARKSWITCH SDLK_d

#define BUTTON_UP_INDEX 0
#define BUTTON_DOWN_INDEX 1
#define BUTTON_LEFT_INDEX 2
#define BUTTON_RIGHT_INDEX 3
#define BUTTON_A_INDEX 4
#define BUTTON_B_INDEX 5
#define BUTTON_MENU_INDEX 6
#define BUTTON_VOLDOWN_INDEX 7
#define BUTTON_VOLUP_INDEX 8
#define BUTTON_SOUNDSWITCH_INDEX 9
#define BUTTON_GLOWSWITCH_INDEX 10
#define BUTTON_DARKSWITCH_INDEX 11

#define BUTTON_COUNT 12

// TinyJoypad's own native resolution is 128x64 (OLED_WIDTH x OLED_HEIGHT,
// see machineDependent.h) - default window picked to match the already-
// shipped Vircon32 build's own 640x360 screen for visual familiarity
// (128*5=640 exactly; the extra vertical space just letterboxes, same as
// that project's own 20px top/bottom bars). The window is resizable and
// the content re-scales/re-centers live (see sdlBackend.c's resize watcher).
#define DEFAULT_WINDOW_WIDTH 640
#define DEFAULT_WINDOW_HEIGHT 360

#endif
