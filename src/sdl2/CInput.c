#include <SDL.h>
#include <SDL_joystick.h>
#include <math.h>
#include <stdbool.h>
#include "CInput.h"
#include "tinyjoypadSDL2.h"


CInput* CInput_Create()
{
	SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
	CInput* Result = (CInput*)SDL_malloc(sizeof(CInput));
	Result->JoystickDeadZone = 10000;
	Result->TriggerDeadZone = 10000;
	Result->GameController = NULL;
	CInput_ResetButtons(Result);
	// SDL2 has no SDL_GetJoysticks()-style array+count query (that's an
	// SDL3 addition) - SDL_NumJoysticks() + an index loop is SDL2's own
	// equivalent, same shape as the sibling cglpSDL2/CInput.c.
	for (int i=0; i < SDL_NumJoysticks(); i++)
	{
		if(SDL_IsGameController(i))
		{
			Result->GameController = SDL_GameControllerOpen(i);
			SDL_GameControllerEventState(SDL_ENABLE);
			SDL_Log("Joystick Detected!\n");
			break;
		}
	}
	return Result;
}

void CInput_Destroy(CInput *Input)
{
	if(Input->GameController)
	{
		SDL_GameControllerClose(Input->GameController);
		Input->GameController = NULL;
	}
	SDL_free(Input);
	Input = NULL;
	SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}


void CInput_HandleJoystickButtonEvent(CInput *Input, int Button, bool Value)
{
	switch (Button)
	{
		// SDL2's own SDL_CONTROLLER_BUTTON_* names are label-based (Y/X/B/A)
		// rather than SDL3's position-based NORTH/WEST/EAST/SOUTH renaming -
		// same physical Xbox-layout mapping either way, so the assignments
		// below (which of our own But* fields each one drives) are
		// unchanged from src/sdl3/CInput.c, only the enum names differ.
		case SDL_CONTROLLER_BUTTON_Y:
			Input->Buttons.ButY = Value;
			break;
		case SDL_CONTROLLER_BUTTON_X:
			Input->Buttons.ButX = Value;
			break;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			Input->Buttons.ButLB = Value;
			break;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
			Input->Buttons.ButRB = Value;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_UP:
			Input->Buttons.ButDpadUp = Value;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			Input->Buttons.ButDpadDown = Value;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			Input->Buttons.ButDpadLeft = Value;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
			Input->Buttons.ButDpadRight = Value;
			break;
		case SDL_CONTROLLER_BUTTON_A:
			Input->Buttons.ButA = Value;
			break;
		case SDL_CONTROLLER_BUTTON_B:
			Input->Buttons.ButB = Value;
			break;
		case SDL_CONTROLLER_BUTTON_START:
			Input->Buttons.ButStart = Value;
			break;
		case SDL_CONTROLLER_BUTTON_BACK:
			Input->Buttons.ButBack = Value;
			break;
		default:
			break;
	}
}

void CInput_HandleKeyboardEvent(CInput *Input, int Key, bool Value)
{
	switch (Key)
	{
		case SDLK_F4:
			Input->Buttons.ButQuit = Value;
			break;
		case SDLK_F3:
			Input->Buttons.ButFullscreen = Value;
			break;
		case BUTTON_VOLUP:
			Input->Buttons.ButRB = Value;
			break;
		case BUTTON_VOLDOWN:
			Input->Buttons.ButLB = Value;
			break;
		case BUTTON_UP:
			Input->Buttons.ButUp = Value;
			break;
		case BUTTON_DOWN:
			Input->Buttons.ButDown = Value;
			break;
		case BUTTON_LEFT:
			Input->Buttons.ButLeft = Value;
			break;
		case BUTTON_RIGHT:
			Input->Buttons.ButRight = Value;
			break;
		case BUTTON_MENU:
			Input->Buttons.ButBack = Value;
			break;
		case BUTTON_A:
			Input->Buttons.ButA = Value;
			break;
		case BUTTON_B:
			Input->Buttons.ButB = Value;
			break;
		case BUTTON_SOUNDSWITCH:
			Input->Buttons.ButY = Value;
			break;
		case BUTTON_GLOWSWITCH:
			Input->Buttons.ButX = Value;
			break;
		case BUTTON_DARKSWITCH:
			Input->Buttons.ButStart = Value;
			break;
		default:
			break;
	}
}

void CInput_HandleJoystickAxisEvent(CInput *Input, int Axis, int Value)
{
	switch(Axis)
	{
		case SDL_CONTROLLER_AXIS_LEFTX:
			if (SDL_abs(Value) < Input->JoystickDeadZone)
			{
				Input->Buttons.ButRight = false;
				Input->Buttons.ButLeft = false;
				return;
			}
			if(Value > 0)
				Input->Buttons.ButRight = true;
			else
				Input->Buttons.ButLeft = true;
			break;

		case SDL_CONTROLLER_AXIS_LEFTY:
			if (SDL_abs(Value) < Input->JoystickDeadZone)
			{
				Input->Buttons.ButUp = false;
				Input->Buttons.ButDown = false;
				return;
			}
			if(Value < 0)
				Input->Buttons.ButUp = true;
			else
				Input->Buttons.ButDown = true;
			break;

		case SDL_CONTROLLER_AXIS_RIGHTX:
			if (SDL_abs(Value) < Input->JoystickDeadZone)
			{
				Input->Buttons.ButRight2 = false;
				Input->Buttons.ButLeft2 = false;
				return;
			}
			if(Value > 0)
				Input->Buttons.ButRight2 = true;
			else
				Input->Buttons.ButLeft2 = true;
			break;

		case SDL_CONTROLLER_AXIS_RIGHTY:
			if (SDL_abs(Value) < Input->JoystickDeadZone)
			{
				Input->Buttons.ButUp2 = false;
				Input->Buttons.ButDown2 = false;
				return;
			}
			if(Value < 0)
				Input->Buttons.ButUp2 = true;
			else
				Input->Buttons.ButDown2 = true;
			break;

		// SDL2's own axis names put LEFT/RIGHT before TRIGGER
		// (TRIGGERLEFT/TRIGGERRIGHT) rather than SDL3's LEFT_TRIGGER/
		// RIGHT_TRIGGER word order - easy to typo, called out explicitly.
		case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
			if (SDL_abs(Value) < Input->TriggerDeadZone)
			{
				Input->Buttons.ButLT = false;
				return;
			}
			Input->Buttons.ButLT = true;
			break;

		case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
			if (SDL_abs(Value) < Input->TriggerDeadZone)
			{
				Input->Buttons.ButRT = false;
				return;
			}
			Input->Buttons.ButRT = true;
			break;
	}
}

void CInput_HandleMouseEvent(CInput *Input, int Button, bool Value)
{
	switch (Button)
	{
		case SDL_BUTTON_LEFT:
			Input->Buttons.ButA = Value;
			break;
		case SDL_BUTTON_RIGHT:
			Input->Buttons.ButB = Value;
			break;
		default:
			break;
	}
}

void CInput_Update(CInput *Input)
{
	SDL_Event Event;
	Input->PrevButtons = Input->Buttons;
	Input->Buttons.ButQuit = false;
	Input->Buttons.RenderReset = false;
	while (SDL_PollEvent(&Event))
	{
		// SDL2's event-type constants are their own SDL_EVENTNAME tokens
		// (no "SDL_EVENT_" prefix, no verb/noun reordering) rather than
		// SDL3's SDL_EVENT_CATEGORY_DETAIL scheme - a straight rename per
		// constant, same event semantics.
		if (Event.type == SDL_RENDER_TARGETS_RESET)
			Input->Buttons.RenderReset = true;

		if (Event.type == SDL_QUIT)
			Input->Buttons.ButQuit = true;

		if (Event.type == SDL_JOYDEVICEADDED)
			if(Input->GameController == NULL)
				if(SDL_IsGameController(Event.jdevice.which))
				Input->GameController = SDL_GameControllerOpen(Event.jdevice.which);

		if (Event.type == SDL_JOYDEVICEREMOVED)
		{
			SDL_Joystick* Joystick = SDL_GameControllerGetJoystick(Input->GameController);
			if (Joystick)
				if (Event.jdevice.which == SDL_JoystickInstanceID(Joystick))
				{
					SDL_GameControllerClose(Input->GameController);
					Input->GameController = NULL;
				}
		}

		if (Event.type == SDL_CONTROLLERAXISMOTION)
			CInput_HandleJoystickAxisEvent(Input, Event.jaxis.axis, Event.jaxis.value);

		if (Event.type == SDL_CONTROLLERBUTTONUP)
			CInput_HandleJoystickButtonEvent(Input, Event.cbutton.button, false);

		if (Event.type == SDL_CONTROLLERBUTTONDOWN)
			CInput_HandleJoystickButtonEvent(Input, Event.cbutton.button, true);

		// event.key.keysym.sym, not SDL3's flattened event.key.key - SDL2
		// still nests the keycode inside a keysym struct.
		if (Event.type == SDL_KEYUP)
			CInput_HandleKeyboardEvent(Input, Event.key.keysym.sym, false);

		if (Event.type == SDL_KEYDOWN)
			CInput_HandleKeyboardEvent(Input, Event.key.keysym.sym, true);

		if (Event.type == SDL_MOUSEBUTTONDOWN)
			CInput_HandleMouseEvent(Input, Event.button.button, true);

		if (Event.type == SDL_MOUSEBUTTONUP)
			CInput_HandleMouseEvent(Input, Event.button.button, false);
	}
	SDL_GetMouseState(&Input->Buttons.MouseX, &Input->Buttons.MouseY);
}

void CInput_ResetButtons(CInput *Input)
{
	Input->Buttons.MouseX = 0;
	Input->Buttons.MouseY = 0;
	Input->Buttons.ButLeft = false;
	Input->Buttons.ButRight = false;
	Input->Buttons.ButUp = false;
	Input->Buttons.ButDown = false;
	Input->Buttons.ButLB = false;
	Input->Buttons.ButRB = false;
	Input->Buttons.ButLT = false;
	Input->Buttons.ButRT = false;
	Input->Buttons.ButBack = false;
	Input->Buttons.ButA = false;
	Input->Buttons.ButB = false;
	Input->Buttons.ButX = false;
	Input->Buttons.ButY = false;
	Input->Buttons.ButStart = false;
	Input->Buttons.ButQuit = false;
	Input->Buttons.ButFullscreen = false;
	Input->Buttons.RenderReset = false;
	Input->Buttons.ButDpadLeft = false;
	Input->Buttons.ButDpadRight = false;
	Input->Buttons.ButDpadUp = false;
	Input->Buttons.ButDpadDown = false;
	Input->Buttons.ButLeft2 = false;
	Input->Buttons.ButRight2 = false;
	Input->Buttons.ButUp2 = false;
	Input->Buttons.ButDown2 = false;
	Input->PrevButtons = Input->Buttons;
}
