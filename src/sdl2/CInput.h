#ifndef CINPUT_H
#define CINPUT_H

#include <SDL.h>
#include <stdbool.h>

struct SButtons
{
	bool ButLeft, ButRight, ButUp, ButDown,
		 ButDpadLeft, ButDpadRight, ButDpadUp, ButDpadDown,
		 ButLeft2, ButRight2, ButUp2, ButDown2,
		 ButBack, ButStart, ButA, ButB,
		 ButX, ButY, ButLB, ButRB, ButFullscreen, ButQuit, ButRT, ButLT,
		 RenderReset;
	// int, not SDL3-flavor float: SDL2's own SDL_GetMouseState() takes
	// int* out-params, not float* - MouseX/Y are otherwise unused anywhere
	// in this project (see CInput.c's own note), so this is a mechanical
	// type match with zero behavioral consequence either way.
	int MouseX, MouseY;
};
typedef struct SButtons SButtons;

struct CInput
{
	// SDL_GameController*, not SDL3-flavor SDL_Gamepad* - SDL2's own
	// gamepad-abstraction type name (SDL3 renamed the whole
	// SDL_GameController* API family to SDL_Gamepad*).
	SDL_GameController* GameController;
	SButtons Buttons, PrevButtons;
	int JoystickDeadZone, TriggerDeadZone;
};
typedef struct CInput CInput;

CInput* CInput_Create();
void CInput_Destroy(CInput *Input);
void CInput_Update(CInput *Input);
void CInput_ResetButtons(CInput *Input);

#endif