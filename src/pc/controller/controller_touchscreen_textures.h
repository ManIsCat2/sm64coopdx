#ifndef CONTROLLER_TOUCHSCREEN_TEXTURES_H
#define CONTROLLER_TOUCHSCREEN_TEXTURES_H
#ifdef TOUCH_CONTROLS
#include "macros.h"
#include "types.h"

#define TEXTURE_TOUCH_JOYSTICK           0
#define TEXTURE_TOUCH_JOYSTICK_BASE      1
#define TEXTURE_TOUCH_C_UP               2
#define TEXTURE_TOUCH_C_UP_PRESSED       3
#define TEXTURE_TOUCH_C_DOWN             4
#define TEXTURE_TOUCH_C_DOWN_PRESSED     5
#define TEXTURE_TOUCH_C_LEFT             6
#define TEXTURE_TOUCH_C_LEFT_PRESSED     7
#define TEXTURE_TOUCH_C_RIGHT            8
#define TEXTURE_TOUCH_C_RIGHT_PRESSED    9
#define TEXTURE_TOUCH_DPAD_UP            10
#define TEXTURE_TOUCH_DPAD_UP_PRESSED    11
#define TEXTURE_TOUCH_DPAD_DOWN          12
#define TEXTURE_TOUCH_DPAD_DOWN_PRESSED  13
#define TEXTURE_TOUCH_DPAD_LEFT          14
#define TEXTURE_TOUCH_DPAD_LEFT_PRESSED  15
#define TEXTURE_TOUCH_DPAD_RIGHT         16
#define TEXTURE_TOUCH_DPAD_RIGHT_PRESSED 17
#define TEXTURE_TOUCH_A                  18
#define TEXTURE_TOUCH_A_PRESSED          19
#define TEXTURE_TOUCH_B                  20
#define TEXTURE_TOUCH_B_PRESSED          21
#define TEXTURE_TOUCH_X                  22
#define TEXTURE_TOUCH_X_PRESSED          23
#define TEXTURE_TOUCH_Y                  24
#define TEXTURE_TOUCH_Y_PRESSED          25
#define TEXTURE_TOUCH_START              26
#define TEXTURE_TOUCH_START_PRESSED      27
#define TEXTURE_TOUCH_L                  28
#define TEXTURE_TOUCH_L_PRESSED          29
#define TEXTURE_TOUCH_R                  30
#define TEXTURE_TOUCH_R_PRESSED          31
#define TEXTURE_TOUCH_Z                  32
#define TEXTURE_TOUCH_Z_PRESSED          33
#define TEXTURE_TOUCH_CHAT               34
#define TEXTURE_TOUCH_CHAT_PRESSED       35
#define TEXTURE_TOUCH_PLAYERLIST         36
#define TEXTURE_TOUCH_PLAYERLIST_PRESSED 37
#define TEXTURE_TOUCH_CONSOLE            38
#define TEXTURE_TOUCH_CONSOLE_PRESSED    39

extern const Texture *const touch_textures[];

#endif
#endif