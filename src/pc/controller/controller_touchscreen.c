//Feel free to use it in your port too, but please keep authorship!
//Touch Controls made by: VDavid003
/* Heavily modified by xLuigiGamerx and ManIsCat2 to add new features, like:

- DJUI Based touch controls editor
- Different texture per button and joystick (joystick base as well)

*/

#ifdef TOUCH_CONTROLS
#include <ultra64.h>
#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "config.h"
#include "sm64.h"
#include "game/game_init.h"
#include "game/memory.h"
#include "game/segment2.h"
#include "game/object_helpers.h"
#include "gfx_dimensions.h"
#include "pc/pc_main.h"
#include "pc/gfx/gfx_pc.h"
#include "pc/djui/djui_panel.h"
#include "pc/djui/djui_panel_pause.h"
#include "pc/djui/djui_panel_main.h"
#include "pc/djui/djui_panel_touch_controls_editor.h"
#include "pc/djui/djui_console.h"
#include "pc/network/network.h"

#include "controller_api.h"
#include "controller_touchscreen.h"
#include "controller_touchscreen_textures.h"

#include "pc/configfile.h"

// Mouselook
s16 before_x = 0;
s16 before_y = 0;
s16 touch_x = 0;
s16 touch_y = 0;
s16 touch_cam_last_x = 0;
s16 touch_cam_last_y = 0;

// Config
bool gInTouchConfig = false, gGamepadActive = false;
enum ConfigControlElementIndex gSelectedTouchElement = TOUCH_MOUSE;

ConfigControlElement configControlElementsDefault[TOUCH_COUNT] = {
#include "controller_touchscreen_layout.inc"
};

ConfigControlElement configControlElements[TOUCH_COUNT] = {
#include "controller_touchscreen_layout.inc"
};

ConfigControlElement configControlElementsLast[TOUCH_COUNT] = {
#include "controller_touchscreen_layout.inc"
};

// This order must match configControlElements and ConfigControlElementIndex
static struct ControlElement controlElements[TOUCH_COUNT] = {
    [TOUCH_STICK] =      {.type = Joystick},
    [TOUCH_MOUSE] =      {.type = Mouse},
    [TOUCH_A] =          {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_A,          .buttonDown = TEXTURE_TOUCH_A_PRESSED },          .buttonID = A_BUTTON},
    [TOUCH_B] =          {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_B,          .buttonDown = TEXTURE_TOUCH_B_PRESSED },          .buttonID = B_BUTTON},
    [TOUCH_X] =          {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_X,          .buttonDown = TEXTURE_TOUCH_X_PRESSED },          .buttonID = X_BUTTON},
    [TOUCH_Y] =          {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_Y,          .buttonDown = TEXTURE_TOUCH_Y_PRESSED },          .buttonID = Y_BUTTON},
    [TOUCH_START] =      {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_START,      .buttonDown = TEXTURE_TOUCH_START_PRESSED },      .buttonID = START_BUTTON},
    [TOUCH_L] =          {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_L,          .buttonDown = TEXTURE_TOUCH_L_PRESSED },          .buttonID = L_TRIG},
    [TOUCH_R] =          {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_R,          .buttonDown = TEXTURE_TOUCH_R_PRESSED },          .buttonID = R_TRIG},
    [TOUCH_Z] =          {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_Z,          .buttonDown = TEXTURE_TOUCH_Z_PRESSED },          .buttonID = Z_TRIG},
    [TOUCH_CUP] =        {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_C_UP,       .buttonDown = TEXTURE_TOUCH_C_UP_PRESSED },       .buttonID = U_CBUTTONS},
    [TOUCH_CDOWN] =      {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_C_DOWN,     .buttonDown = TEXTURE_TOUCH_C_DOWN_PRESSED },     .buttonID = D_CBUTTONS},
    [TOUCH_CLEFT] =      {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_C_LEFT,     .buttonDown = TEXTURE_TOUCH_C_LEFT_PRESSED },     .buttonID = L_CBUTTONS},
    [TOUCH_CRIGHT] =     {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_C_RIGHT,    .buttonDown = TEXTURE_TOUCH_C_RIGHT_PRESSED },    .buttonID = R_CBUTTONS},
    [TOUCH_CHAT] =       {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_CHAT,       .buttonDown = TEXTURE_TOUCH_CHAT_PRESSED },       .buttonID = CHAT_BUTTON},
    [TOUCH_PLAYERLIST] = {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_PLAYERLIST, .buttonDown = TEXTURE_TOUCH_PLAYERLIST_PRESSED }, .buttonID = PLAYERLIST_BUTTON},
    [TOUCH_DUP] =        {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_DPAD_UP,    .buttonDown = TEXTURE_TOUCH_DPAD_UP_PRESSED },    .buttonID = U_JPAD},
    [TOUCH_DDOWN] =      {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_DPAD_DOWN,  .buttonDown = TEXTURE_TOUCH_DPAD_DOWN_PRESSED },  .buttonID = D_JPAD},
    [TOUCH_DLEFT] =      {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_DPAD_LEFT,  .buttonDown = TEXTURE_TOUCH_DPAD_LEFT_PRESSED },  .buttonID = L_JPAD},
    [TOUCH_DRIGHT] =     {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_DPAD_RIGHT, .buttonDown = TEXTURE_TOUCH_DPAD_RIGHT_PRESSED }, .buttonID = R_JPAD},
    [TOUCH_CONSOLE] =    {.type = Button, .buttonTexture = { .buttonUp = TEXTURE_TOUCH_CONSOLE,    .buttonDown = TEXTURE_TOUCH_CONSOLE_PRESSED },    .buttonID = CONSOLE_BUTTON},
};

static u32 controlElementsLength = sizeof(controlElements)/sizeof(struct ControlElement);

struct Position get_pos(ConfigControlElement *config) {
    struct Position ret;

    switch (config->anchor) {
        case CONTROL_ELEMENT_LEFT:
            ret.x = RECT_FROM_LEFT_EDGE(config->x << 2);
            ret.y = config->y;
            break;

        case CONTROL_ELEMENT_RIGHT:
            ret.x = RECT_FROM_RIGHT_EDGE(config->x << 2);
            ret.y = config->y;
            break;

        case CONTROL_ELEMENT_CENTER:
            ret.x = SCREEN_WIDTH_API / 2;
            ret.y = config->y;
            break;
    }

    if (config->hidden) {
        if (!gInTouchConfig) {
            ret.x = HIDE_POS;
            ret.y = HIDE_POS;
        }
    }

    if (configSnapTouch) {
        ret.x = 50 * ((ret.x + 49) / 50) - 25;
        ret.y = 50 * ((ret.y + 49) / 50) - 25;
    }

    if (!gInTouchConfig && (gDjuiInMainMenu && !gDjuiDisabled)) {
        ret.x = HIDE_POS;
        ret.y = HIDE_POS;
    }

    return ret;
}

Colors get_color(ConfigControlElement *config) {
    Colors ret;
    
    ret.r = config->r;
    ret.g = config->g;
    ret.b = config->b;
    ret.a = config->a;

    return ret;
}

void move_touch_element(struct TouchEvent *event, enum ConfigControlElementIndex i) {
    ConfigControlElement *config = &configControlElements[i];

    s32 x_raw = CORRECT_TOUCH_X(event->x);
    s32 y_raw = CORRECT_TOUCH_Y(event->y);

    s32 x = 0;
    enum ConfigControlElementAnchor anchor;

    if (x_raw < (SCREEN_WIDTH_API / 2) - 30) {
        x = (x_raw - RECT_FROM_LEFT_EDGE(0)) >> 2;
        if (x < 0) x = 0;
        anchor = CONTROL_ELEMENT_LEFT;
    } else if (x_raw > (SCREEN_WIDTH_API / 2) + 30) {
        x = (RECT_FROM_RIGHT_EDGE(0) - x_raw) >> 2;
        if (x < 0) x = 0;
        anchor = CONTROL_ELEMENT_RIGHT;
    } else {
        x = SCREEN_WIDTH_API / 2;
        anchor = CONTROL_ELEMENT_CENTER;
    }

    config->x = x;
    config->y = y_raw;
    config->anchor = anchor;
}

void touch_down(struct TouchEvent* event) {
    gGamepadActive = false;
    struct Position pos;
    s32 size;
    for(u32 i = 0; i < controlElementsLength; i++) {
        if (controlElements[i].touchID == 0) {
            pos = get_pos(&configControlElements[i]);
            if (pos.y == HIDE_POS) continue;
            size = configControlElements[i].size * 100;
            // The joystick is grabbable across its whole visible base (a circle
            // matching the rendered base), not just the central knob. Other
            // elements keep the original square hit box.
            if (controlElements[i].type == Joystick) {
                f32 hdx = CORRECT_TOUCH_X(event->x) - pos.x;
                f32 hdy = CORRECT_TOUCH_Y(event->y) - pos.y;
                // render_texture draws the 32px base at scaling (1 + size), so its
                // radius in API space is (32 << (1 + size)). Match that for grab.
                f32 grabRadius = (f32)(32 << (1 + configControlElements[i].size));
                if (hdx * hdx + hdy * hdy > grabRadius * grabRadius) continue;
            } else if (!TRIGGER_DETECT(size)) {
                continue;
            }
            switch (controlElements[i].type) {
                case Joystick:
                    controlElements[i].touchID = event->touchID;
                    gSelectedTouchElement = i;
                    if (!gInTouchConfig) {
                        // Radial clamp so no direction is preferred (a real analog
                        // stick has a circular gate). Functional radius is size/2,
                        // which the N64 stick mapping scales to full tilt.
                        f32 jx = CORRECT_TOUCH_X(event->x) - pos.x;
                        f32 jy = CORRECT_TOUCH_Y(event->y) - pos.y;
                        f32 clampR = size / 2.0f;
                        f32 jmag = sqrtf(jx * jx + jy * jy);
                        if (jmag > clampR) {
                            f32 s = clampR / jmag;
                            jx *= s; jy *= s;
                        }
                        controlElements[i].joyX = (s32)jx;
                        controlElements[i].joyY = (s32)jy;
                    }
                    break;
                case Mouse:
                    controlElements[i].touchID = event->touchID;
                    break;
                case Button:
                    controlElements[i].touchID = event->touchID;
                    gSelectedTouchElement = i;
                    // messy
                    if (controlElements[i].buttonID == CHAT_BUTTON && !gInTouchConfig)
                        djui_interactable_on_key_down(configKeyChat[0]);
                    if (controlElements[i].buttonID == PLAYERLIST_BUTTON && !gInTouchConfig)
                        djui_interactable_on_key_down(configKeyPlayerList[0]);
                    break;
            }
        }
    }
}

void touch_motion(struct TouchEvent* event) {
    struct Position pos;
    s32 size;
    for(u32 i = 0; i < controlElementsLength; i++) {
        pos = get_pos(&configControlElements[i]);
        if (pos.y == HIDE_POS) continue;
        size = configControlElements[i].size * 100;
        if (gInTouchConfig) {
            if (controlElements[i].touchID == event->touchID && controlElements[i].type != Mouse && gSelectedTouchElement == i) {
                move_touch_element(event, gSelectedTouchElement);
            }
        } else {
            if (!gDjuiPanelPauseCreated) {
                if (controlElements[i].touchID == event->touchID) {
                    s32 x, y;
                    switch (controlElements[i].type) {
                        case Joystick:
                            if (configPhantomTouch && !TRIGGER_DETECT(size * 6)) {
                                controlElements[i].joyX = 0;
                                controlElements[i].joyY = 0;
                                controlElements[i].touchID = 0;
                                break;
                            }
                            x = CORRECT_TOUCH_X(event->x) - pos.x;
                            y = CORRECT_TOUCH_Y(event->y) - pos.y;
                            // Radial clamp to a circular gate so diagonals are not
                            // preferred over cardinals. The original per-axis clamp
                            // let corners reach ~1.41x magnitude, pulling the stick
                            // toward diagonal directions.
                            {
                                f32 fx = x, fy = y;
                                f32 clampR = size / 2.0f;
                                f32 mag = sqrtf(fx * fx + fy * fy);
                                if (mag > clampR) {
                                    f32 s = clampR / mag;
                                    fx *= s; fy *= s;
                                }
                                controlElements[i].joyX = (s32)fx;
                                controlElements[i].joyY = (s32)fy;
                            }
                            break;
                        case Mouse:
                            if (configPhantomTouch && !TRIGGER_DETECT(size)) {
                                touch_x = before_x = 0;
                                touch_y = before_y = 0;
                                controlElements[i].touchID = 0;
                                break;
                            }
                            if (before_x > 0)
                                touch_x = CORRECT_TOUCH_X(event->x) - before_x;
                            if (before_y > 0)
                                touch_y = CORRECT_TOUCH_Y(event->y) - before_y;
                            before_x = CORRECT_TOUCH_X(event->x);
                            before_y = CORRECT_TOUCH_Y(event->y);
                            if ((u16)abs(touch_x) < configStickDeadzone / 4)
                                touch_x = 0;
                            if ((u16)abs(touch_y) < configStickDeadzone / 4)
                                touch_y = 0;
                            break;
                        case Button:
                            if ((controlElements[i].slideTouch && !TRIGGER_DETECT(size)) || (configPhantomTouch && !controlElements[i].slideTouch && !TRIGGER_DETECT(size * 3))) {
                                controlElements[i].slideTouch = 0;
                                controlElements[i].touchID = 0;
                            }
                            break;
                    }
                } else if ((TRIGGER_DETECT(size) || (configPhantomTouch && TRIGGER_DETECT(size * 6) && controlElements[i].type == Joystick)) && (controlElements[TOUCH_MOUSE].touchID != event->touchID || !configFreeCameraMouse) && configSlideTouch) {
                    if (configPhantomTouch)
                        controlElements[i].touchID = event->touchID;
                    switch (controlElements[i].type) {
                        case Joystick:
                            break;
                        case Mouse:
                            break;
                        case Button:
                            controlElements[i].slideTouch = 1;
                            controlElements[i].touchID = event->touchID;
                            if (controlElements[i].buttonID == CHAT_BUTTON)
                                djui_interactable_on_key_down(configKeyChat[0]);
                            if (controlElements[i].buttonID == PLAYERLIST_BUTTON)
                                djui_interactable_on_key_down(configKeyPlayerList[0]);
                            break;
                    }
                }
            }
        }
    }
}

static void handle_touch_up(u32 i) { // separated for when the layout changes
    controlElements[i].touchID = 0;
    struct Position pos = get_pos(&configControlElements[i]);
    if (pos.y == HIDE_POS) { return; }
    switch (controlElements[i].type) {
        case Joystick:
            controlElements[i].joyX = 0;
            controlElements[i].joyY = 0;
            break;
        case Mouse:
            touch_x = before_x = 0;
            touch_y = before_y = 0;
            break;
        case Button:
            if (controlElements[i].buttonID == CHAT_BUTTON && !gInTouchConfig)
                djui_interactable_on_key_up(configKeyChat[0]);
            if (controlElements[i].buttonID == PLAYERLIST_BUTTON && !gInTouchConfig)
                djui_interactable_on_key_up(configKeyPlayerList[0]);
            if (controlElements[i].buttonID == CONSOLE_BUTTON && !gInTouchConfig)
                djui_console_toggle();
            break;
    }
}

void touch_up(struct TouchEvent* event) {
    for(u32 i = 0; i < controlElementsLength; i++) {
        if (controlElements[i].touchID == event->touchID) {
            handle_touch_up(i);
        }
    }
}

static void render_texture(const Texture *texture, s32 x, s32 y, u32 w, u32 h, s32 scaling, u8 r, u8 g, u8 b, u8 a) {
    gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING);
    gDPSetCombineMode(gDisplayListHead++, G_CC_FADEA, G_CC_FADEA);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetTextureFilter(gDisplayListHead++, G_TF_POINT);
    gSPTexture(gDisplayListHead++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPLoadTextureBlock(gDisplayListHead++, texture, G_IM_FMT_RGBA, G_IM_SIZ_16b, w, h, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0);
    gDPSetEnvColor(gDisplayListHead++, r, g, b, a);

    s32 half_w = (w << scaling);
    s32 half_h = (h << scaling);
    gSPTextureRectangle(gDisplayListHead++, x - half_w, y - half_h, x + half_w, y + half_h, G_TX_RENDERTILE, 0, 0, 4 << (9 - scaling), 1 << (11 - scaling));
    gSPTexture(gDisplayListHead++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
}


void render_touch_controls(void) {
    if ((gGamepadActive && configAutohideTouch) || (!gDjuiInMainMenu && gDjuiDisabled)) { return; }

    struct Position pos;
    struct Position stick;
    Colors color;
    s32 size;
    f32 normalizedVectorMultiplier;
    
    create_dl_ortho_matrix();

    for (u32 i = 0; i < controlElementsLength; i++) {
        pos = get_pos(&configControlElements[i]);
        color = get_color(&configControlElements[i]);
        size = configControlElements[i].size;
        if (pos.y == HIDE_POS) continue;
        switch (controlElements[i].type) {
            case Joystick:
                if (absi(controlElements[i].joyX) + absi(controlElements[i].joyY) != 0) {
                    normalizedVectorMultiplier = sqrt((controlElements[i].joyX * controlElements[i].joyX) + (controlElements[i].joyY * controlElements[i].joyY))/(absi(controlElements[i].joyX) + absi(controlElements[i].joyY));
                } else {
                    normalizedVectorMultiplier = 0;
                }
                if (gInTouchConfig || gDjuiPanelPauseCreated) {
                    stick.x = 0;
                    stick.y = 0;
                } else {
                    stick.x = (controlElements[i].joyX * normalizedVectorMultiplier * 2);
                    stick.y = (controlElements[i].joyY * normalizedVectorMultiplier * 2);
                }
                render_texture(touch_textures[TEXTURE_TOUCH_JOYSTICK_BASE], pos.x, pos.y, 32, 32, 1 + size, color.r, color.g, color.b, color.a);
                render_texture(touch_textures[TEXTURE_TOUCH_JOYSTICK], pos.x + stick.x, pos.y + stick.y, 16, 16, 1 + size, color.r, color.g, color.b, color.a);
                break;
            case Mouse:
                break;
            case Button:
                if (!controlElements[i].touchID || gInTouchConfig || gDjuiPanelPauseCreated) {
                    render_texture(touch_textures[controlElements[i].buttonTexture.buttonUp], pos.x, pos.y, 16, 16, 1 + size, color.r, color.g, color.b, color.a);
                } else {
                    render_texture(touch_textures[controlElements[i].buttonTexture.buttonDown], pos.x, pos.y, 16, 16, 1 + size, color.r, color.g, color.b, color.a);
                }
                break;
        }
    }
}

static void touchscreen_init(void) {
    for (u32 i = 0; i < controlElementsLength; i++) {
        controlElements[i].touchID = 0;
        controlElements[i].joyX = 0;
        controlElements[i].joyY = 0;
        controlElements[i].slideTouch = 0;
    }
}

static void touchscreen_read(OSContPad *pad) {
    struct Position pos;
    s32 size;
    if (!gInTouchConfig && !gDjuiPanelPauseCreated) {
        for(u32 i = 0; i < controlElementsLength; i++) {
            pos = get_pos(&configControlElements[i]);
            size = configControlElements[i].size * 100;
            if (pos.y == HIDE_POS) continue;
            switch (controlElements[i].type) {
                case Joystick:
                    if (controlElements[i].joyX || controlElements[i].joyY) {
                        pad->stick_x = (controlElements[i].joyX + size / 2) * 255 / size - 128;
                        pad->stick_y = (-controlElements[i].joyY + size / 2) * 255 / size - 128; //inverted for some reason
                    }
                    break;
                case Mouse:
                    break;
                case Button:
                    if (controlElements[i].touchID && controlElements[i].buttonID != CHAT_BUTTON && controlElements[i].buttonID != PLAYERLIST_BUTTON && controlElements[i].buttonID != CONSOLE_BUTTON) {
                        pad->button |= controlElements[i].buttonID;
                    }
                    break;
            }
        }
    }
}

// Used by other controller types for setting keybinds
// Doesn't make a huge amount of sense for a touchscreen,
// So instead I allow customizing all button positions in
// an entirely separate construction, which is fine for now
// until someone wants multiple copies of the same button,
// at which point I will have to decide how to do that
static u32 touchscreen_rawkey(void) { 
    return VK_INVALID;
}

struct ControllerAPI controller_touchscreen = {
    0,
    touchscreen_init,
    touchscreen_read,
    touchscreen_rawkey,
    NULL,
    NULL,
    NULL,
    NULL
};
#endif
