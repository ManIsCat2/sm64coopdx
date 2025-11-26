#ifndef CONTROLLER_TOUCHSCREEN_H
#define CONTROLLER_TOUCHSCREEN_H
#ifdef TOUCH_CONTROLS
#include <stdbool.h>
#include "controller_api.h"

#define VK_BASE_TOUCHSCREEN 0x0000

#define PLAYERLIST_BUTTON 0x000F
#define CHAT_BUTTON       0x001C
#define CONSOLE_BUTTON    0x0005

#define SCANCODE_BACK 0

#define MAX_BUTTON_STATES 2

#define TOUCH_STICK      0
#define TOUCH_MOUSE      1
#define TOUCH_A          2
#define TOUCH_B          3
#define TOUCH_X          4
#define TOUCH_Y          5
#define TOUCH_START      6
#define TOUCH_L          7
#define TOUCH_R          8
#define TOUCH_Z          9
#define TOUCH_CUP        10
#define TOUCH_CDOWN      11
#define TOUCH_CLEFT      12
#define TOUCH_CRIGHT     13    
#define TOUCH_CHAT       14
#define TOUCH_PLAYERLIST 15        
#define TOUCH_DUP        16
#define TOUCH_DDOWN      17
#define TOUCH_DLEFT      18
#define TOUCH_DRIGHT     19    
#define TOUCH_CONSOLE    20    
#define TOUCH_COUNT      21

typedef struct {
    uint32_t rawX, rawY, x, y;
    float_t size;
    uint8_t r, g, b, a;
    bool hidden;
} ConfigTouchControl;

extern ConfigTouchControl configTouchControls[TOUCH_COUNT];

extern int16_t touch_x;
extern int16_t touch_y;

extern bool gInTouchConfig, gGamepadActive;
extern uint32_t gTouchControlSelected;

struct Position {
    int x, y;
};

typedef struct {
    int8_t r, g, b, a;
} Colors;

#define TOUCH_TYPE_JOYSTICK 0
#define TOUCH_TYPE_PAD      1
#define TOUCH_TYPE_BUTTON   2

struct TouchControl {
    /* Misc */

    int type;
    int64_t touchID;

    /* Joystick */

    int joyX, joyY;

    /* Button */

    int buttonID;
    int buttonState[MAX_BUTTON_STATES];
    int slideTouch;
};

void touch_down(f32 x, f32 y, int64_t id);
void touch_motion(f32 x, f32 y, int64_t id);
void touch_up(f32 x, f32 y, int64_t id);

void render_touch_controls(void);

extern struct ControllerAPI controller_touchscreen;
#endif
#endif
