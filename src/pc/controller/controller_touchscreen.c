#ifdef TOUCH_CONTROLS
#include <stdbool.h>
#include <ultra64.h>
#include <PR/gbi.h>
#include <time.h>
#include <string.h>
#include "pc/configfile.h"
#include "controller_api.h"

#include "controller_touchscreen.h"

#include "config.h"
#include "sm64.h"
#include "game/game_init.h"
#include "gfx_dimensions.h"
#include "pc/pc_main.h"
#include "pc/network/network.h"
#include "pc/djui/djui_gfx.h"
#include "pc/djui/djui_panel.h"
#include "pc/djui/djui_panel_pause.h"
#include "pc/djui/djui_panel_touch_controls_editor.h"
#include "pc/djui/djui_console.h"

#include "controller_touchscreen_textures.h"

// RAPI

struct GfxRenderingAPI *r_api = &RAPI;

// Macros

#define LEFT_EDGE ((int)floorf(GFX_DIMENSIONS_FROM_LEFT_EDGE(0)))
#define RIGHT_EDGE ((int)ceilf(GFX_DIMENSIONS_FROM_RIGHT_EDGE(0)))

#define SCALE_X(x) ((x * (RIGHT_EDGE - LEFT_EDGE)) + LEFT_EDGE)
#define SCALE_Y(y) (y * SCREEN_HEIGHT)

#define TOUCH_DETECT(x, y, tx, ty, size) ((tx < (x + size / 2)) && (tx > (x - size / 2)) && (ty < (y + size / 2)) && (ty > (y - size / 2)))

// Touch Cam

s16 before_x = 0;
s16 before_y = 0;
s16 touch_x = 0;
s16 touch_y = 0;

// Config

bool gInTouchConfig = false, gGamepadActive = false;
uint32_t gTouchControlSelected = TOUCH_MOUSE;

ConfigTouchControl configTouchControls[TOUCH_COUNT] = {
#include "controller_touchscreen_layout.inc"
};

static struct TouchControl touchControls[TOUCH_COUNT] = {
    [TOUCH_STICK] =      { .type = TOUCH_TYPE_JOYSTICK                                                                                                             },
    [TOUCH_MOUSE] =      { .type = TOUCH_TYPE_PAD                                                                                                                  },
    [TOUCH_A] =          { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_A,          TEXTURE_TOUCH_A_PRESSED          }, .buttonID = A_BUTTON          },
    [TOUCH_B] =          { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_B,          TEXTURE_TOUCH_B_PRESSED          }, .buttonID = B_BUTTON          },
    [TOUCH_X] =          { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_X,          TEXTURE_TOUCH_X_PRESSED          }, .buttonID = X_BUTTON          },
    [TOUCH_Y] =          { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_Y,          TEXTURE_TOUCH_Y_PRESSED          }, .buttonID = Y_BUTTON          },
    [TOUCH_START] =      { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_START,      TEXTURE_TOUCH_START_PRESSED      }, .buttonID = START_BUTTON      },
    [TOUCH_L] =          { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_L,          TEXTURE_TOUCH_L_PRESSED          }, .buttonID = L_TRIG            },
    [TOUCH_R] =          { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_R,          TEXTURE_TOUCH_R_PRESSED          }, .buttonID = R_TRIG            },
    [TOUCH_Z] =          { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_Z,          TEXTURE_TOUCH_Z_PRESSED          }, .buttonID = Z_TRIG            },
    [TOUCH_CUP] =        { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_C_UP,       TEXTURE_TOUCH_C_UP_PRESSED       }, .buttonID = U_CBUTTONS        },
    [TOUCH_CDOWN] =      { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_C_DOWN,     TEXTURE_TOUCH_C_DOWN_PRESSED     }, .buttonID = D_CBUTTONS        },
    [TOUCH_CLEFT] =      { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_C_LEFT,     TEXTURE_TOUCH_C_LEFT_PRESSED     }, .buttonID = L_CBUTTONS        },
    [TOUCH_CRIGHT] =     { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_C_RIGHT,    TEXTURE_TOUCH_C_RIGHT_PRESSED    }, .buttonID = R_CBUTTONS        },
    [TOUCH_CHAT] =       { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_CHAT,       TEXTURE_TOUCH_CHAT_PRESSED       }, .buttonID = CHAT_BUTTON       },
    [TOUCH_PLAYERLIST] = { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_PLAYERLIST, TEXTURE_TOUCH_PLAYERLIST_PRESSED }, .buttonID = PLAYERLIST_BUTTON },
    [TOUCH_DUP] =        { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_DPAD_UP,    TEXTURE_TOUCH_DPAD_UP_PRESSED    }, .buttonID = U_JPAD            },
    [TOUCH_DDOWN] =      { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_DPAD_DOWN,  TEXTURE_TOUCH_DPAD_DOWN_PRESSED  }, .buttonID = D_JPAD            },
    [TOUCH_DLEFT] =      { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_DPAD_LEFT,  TEXTURE_TOUCH_DPAD_LEFT_PRESSED  }, .buttonID = L_JPAD            },
    [TOUCH_DRIGHT] =     { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_DPAD_RIGHT, TEXTURE_TOUCH_DPAD_RIGHT_PRESSED }, .buttonID = R_JPAD            },
    [TOUCH_CONSOLE] =    { .type = TOUCH_TYPE_BUTTON, .buttonState = { TEXTURE_TOUCH_CONSOLE,    TEXTURE_TOUCH_CONSOLE_PRESSED    }, .buttonID = CONSOLE_BUTTON    },
};

struct Position get_pos(ConfigTouchControl *config) {
    struct Position ret;

    config->x = config->rawX;
    config->y = config->rawY;

    if (config->rawX < SCREEN_WIDTH / 2) {
        config->x = GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(config->rawX);
    } else if (config->rawX > SCREEN_WIDTH / 2) {
        config->x = GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(config->rawX);        
    }

    ret.x = config->x;
    ret.y = config->y;

    if (configSnapTouch) {
        ret.x = 50 * ((ret.x + 49) / 50) - 25;
        ret.y = 50 * ((ret.y + 49) / 50) - 25;
    }

    return ret;
}

Colors get_color(ConfigTouchControl *config) {
    Colors ret;
    
    ret.r = config->r;
    ret.g = config->g;
    ret.b = config->b;
    ret.a = config->a;

    return ret;
}

void touch_control_move(f32 x, f32 y, int i) {
    ConfigTouchControl *config = &configTouchControls[i];

    config->rawX = x;
    config->rawY = y;

    config->x = config->rawX;
    config->y = config->rawY;

    if (config->rawX < SCREEN_WIDTH / 2) {
        config->x = GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(config->rawX);
    } else if (config->rawX > SCREEN_WIDTH / 2) {
        config->x = GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(config->rawX);        
    }
}

void touch_down(f32 x, f32 y, int64_t id) {

    gGamepadActive = false;

    struct Position touchPos;
    touchPos.x = SCALE_X(x);
    touchPos.y = SCALE_Y(y);

    struct Position pos;
    s32 size;
    for (uint32_t i = 0; i < TOUCH_COUNT; i++) {
        struct TouchControl *control = &touchControls[i];
        ConfigTouchControl config = configTouchControls[i];
        if (config.hidden) continue;
        if (control->touchID == 0) {
            pos = get_pos(&config);
            size = config.size * 100;
            bool touched = TOUCH_DETECT(pos.x, pos.y, touchPos.x, touchPos.y, size);
            if (!touched) continue;
            control->touchID = id;
            if (control->type != TOUCH_TYPE_PAD) {
                gTouchControlSelected = i;
                djui_panel_touch_controls_editor_update();
            }
            switch (control->type) {
                case TOUCH_TYPE_JOYSTICK:
                    if (!gInTouchConfig) {
                        control->joyX = touchPos.x - pos.x;
                        control->joyY = touchPos.y - pos.y;
                    }
                    break;
                case TOUCH_TYPE_BUTTON:
                    if (control->buttonID == CHAT_BUTTON && !gInTouchConfig)
                        djui_interactable_on_key_down(configKeyChat[0]);
                    if (control->buttonID == PLAYERLIST_BUTTON && !gInTouchConfig)
                        djui_interactable_on_key_down(configKeyPlayerList[0]);
                    break;
                case TOUCH_TYPE_PAD:
                    break;
            }
        }
    }
}

void touch_motion(f32 x, f32 y, int64_t id) {

    struct Position touchPos;
    touchPos.x = SCALE_X(x);
    touchPos.y = SCALE_Y(y);

    struct Position pos;
    s32 size;
    for (uint32_t i = 0; i < TOUCH_COUNT; i++) {
        struct TouchControl *control = &touchControls[i];
        ConfigTouchControl config = configTouchControls[i];
        if (config.hidden) continue;
        pos = get_pos(&config);
        size = config.size * 100;
        if (gInTouchConfig) {
            if (control->touchID == id && control->type != TOUCH_TYPE_PAD && gTouchControlSelected == i) {
                touch_control_move(touchPos.x, touchPos.y, gTouchControlSelected);
            }
        } else {
            if (!gDjuiPanelPauseCreated) {
                bool touched = TOUCH_DETECT(pos.x, pos.y, touchPos.x, touchPos.y, size);
                if (control->touchID == id) {
                    switch (control->type) {
                        case TOUCH_TYPE_JOYSTICK:
                            s32 joyX, joyY;
                            if (configPhantomTouch && !touched) {
                                control->joyX = 0;
                                control->joyY = 0;
                                control->touchID = 0;
                                break;
                            }
                            joyX = touchPos.x - pos.x;
                            joyY = touchPos.y - pos.y;
                            if (pos.x + size / 2 < touchPos.x) joyX =  size / 2;
                            if (pos.x - size / 2 > touchPos.x) joyX = -size / 2;
                            if (pos.y + size / 2 < touchPos.y) joyY =  size / 2;
                            if (pos.y - size / 2 > touchPos.y) joyY = -size / 2;
                            control->joyX = joyX;
                            control->joyY = joyY;
                            break;
                        case TOUCH_TYPE_PAD:
                            if (configPhantomTouch && !touched) {
                                touch_x = before_x = 0;
                                touch_y = before_y = 0;
                                control->touchID = 0;
                                break;
                            }
                            if (before_x > 0) touch_x = touchPos.x - before_x;
                            if (before_y > 0) touch_y = touchPos.y - before_y;
                            before_x = touchPos.x;
                            before_y = touchPos.y;
                            break;
                        case TOUCH_TYPE_BUTTON:
                            if ((control->slideTouch && !touched) || (configPhantomTouch && !control->slideTouch && !touched)) {
                                control->slideTouch = 0;
                                control->touchID = 0;
                            }
                            break;
                    }
                } else if ((touched || (configPhantomTouch && touched && control->type == TOUCH_TYPE_JOYSTICK)) && (touchControls[TOUCH_MOUSE].touchID != id || !configFreeCameraMouse) && configSlideTouch) {
                    if (configPhantomTouch) control->touchID = id;
                    switch (control->type) {
                        case TOUCH_TYPE_BUTTON:
                            control->slideTouch = 1;
                            control->touchID = id;

                            if (control->buttonID == CHAT_BUTTON) djui_interactable_on_key_down(configKeyChat[0]);
                            if (control->buttonID == PLAYERLIST_BUTTON) djui_interactable_on_key_down(configKeyPlayerList[0]);
                            break;
                        case TOUCH_TYPE_JOYSTICK:
                        case TOUCH_TYPE_PAD:
                            break;
                    }
                }
            }
        }
    }
}

void touch_up(f32 x, f32 y, int64_t id) {
    for (uint32_t i = 0; i < TOUCH_COUNT; i++) {
        struct TouchControl *control = &touchControls[i];
        if (control->touchID == id) {
            ConfigTouchControl config = configTouchControls[i];
            control->touchID = 0;
            if (config.hidden) { return; }
            switch (control->type) {
                case TOUCH_TYPE_JOYSTICK:
                    control->joyX = 0;
                    control->joyY = 0;
                    break;
                case TOUCH_TYPE_PAD:
                    touch_x = before_x = 0;
                    touch_y = before_y = 0;
                    break;
                case TOUCH_TYPE_BUTTON:
                    if (!gInTouchConfig) {
                        if (control->buttonID == CONSOLE_BUTTON) djui_interactable_on_key_up(configKeyConsole[0]);
                        if (control->buttonID == PLAYERLIST_BUTTON) djui_interactable_on_key_up(configKeyPlayerList[0]);
                    }
                    break;
            }
        }
    }
}

static void djui_render_texture(const Texture *texture, f32 x, f32 y, u32 width, u32 height, f32 scale, u8 r, u8 g, u8 b, u8 a) {
    if (!texture) return;

    gDPSetEnvColor(gDisplayListHead++, r, g, b, a);

    u32 windowWidth, windowHeight;
    wm_api->get_dimensions(&windowWidth, &windowHeight);

    f32 widthRatio = (f32)windowWidth  / (f32)SCREEN_WIDTH;
    f32 heightRatio = (f32)windowHeight / (f32)SCREEN_HEIGHT;
    f32 windowScale = fminf(widthRatio, heightRatio);

    f32 offsetX = (windowWidth - SCREEN_WIDTH * windowScale) * 0.5f;
    f32 offsetY = (windowHeight - SCREEN_HEIGHT * windowScale) * 0.5f;

    // translate position
    f32 translatedX = (offsetX + x) * windowScale;
    f32 translatedY = (offsetY + (SCREEN_HEIGHT - y)) * windowScale;
    create_dl_translation_matrix(DJUI_MTX_PUSH, translatedX / windowScale, translatedY / windowScale, 0.0f);

    f32 translatedS = scale;
    djui_gfx_size_translate(&translatedS);

    // translate scale
    create_dl_scale_matrix(DJUI_MTX_NOPUSH, width * translatedS, height * translatedS, 1.0f);

    // render
    djui_gfx_render_texture(texture, width, height, G_IM_FMT_RGBA, G_IM_SIZ_16b, false);

    // pop
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

void render_touch_controls(void) {
    if ((gGamepadActive && configAutohideTouch) || (gDjuiInMainMenu && !gDjuiDisabled)) { return; }

    u32 windowWidth, windowHeight;
    wm_api->get_dimensions(&windowWidth, &windowHeight);
    r_api->set_viewport(0, 0, windowWidth, windowHeight);

    struct Position pos;
    struct Position normalizedStick;
    struct Position stick;
    Colors color;
    f32 size;
    f32 stickMag;
    
    create_dl_ortho_matrix();

    for (uint32_t i = 0; i < TOUCH_COUNT; i++) {
        struct TouchControl control = touchControls[i];
        ConfigTouchControl config = configTouchControls[i];
        pos = get_pos(&config);
        color = get_color(&config);
        size = config.size;
        if (config.hidden && !gInTouchConfig) continue;
        switch (control.type) {
            case TOUCH_TYPE_JOYSTICK:
                normalizedStick.x = 0;
                normalizedStick.y = 0;
                stickMag = sqrt((control.joyX * control.joyX) + (control.joyY * control.joyY));
                if (stickMag != 0) {
                    normalizedStick.x = control.joyX / stickMag;
                    normalizedStick.y = control.joyY / stickMag;
                }
                if (gInTouchConfig || gDjuiPanelPauseCreated) {
                    stick.x = 0;
                    stick.y = 0;
                } else {
                    stick.x = normalizedStick.x;
                    stick.y = normalizedStick.y;
                }
                djui_render_texture(touch_textures[TEXTURE_TOUCH_JOYSTICK_BASE], pos.x, pos.y, 32, 32, size + 4.0f, color.r, color.g, color.b, color.a);
                djui_render_texture(touch_textures[TEXTURE_TOUCH_JOYSTICK], pos.x + stick.x, pos.y + stick.y, 16, 16, size + 4.0f, color.r, color.g, color.b, color.a);
                break;
            case TOUCH_TYPE_BUTTON:
                bool pressed = !control.touchID || gInTouchConfig || gDjuiPanelPauseCreated;
                djui_render_texture(touch_textures[control.buttonState[pressed]], pos.x, pos.y, 16, 16, size + 4.0f, color.r, color.g, color.b, color.a);
                break;
            case TOUCH_TYPE_PAD:
                break;
        }
    }
}

static void touchscreen_init(void) {
    for (uint32_t i = 0; i < TOUCH_COUNT; i++) {
        struct TouchControl *control = &touchControls[i];
        control->touchID = 0;
        control->joyX = 0;
        control->joyY = 0;
        control->slideTouch = 0;
    }
}

static void touchscreen_read(OSContPad *pad) {
    s32 size;
    if (!gInTouchConfig && !gDjuiPanelPauseCreated) {
        for (uint32_t i = 0; i < TOUCH_COUNT; i++) {
            struct TouchControl control = touchControls[i];
            ConfigTouchControl config = configTouchControls[i];
            size = config.size * 100;
            if (config.hidden) continue;
            switch (control.type) {
                case TOUCH_TYPE_JOYSTICK:
                    if (control.joyX || control.joyY) {
                        pad->stick_x = (control.joyX + size / 2) * 255 / size - 128;
                        pad->stick_y = (-control.joyY + size / 2) * 255 / size - 128; //inverted for some reason
                    }
                    break;
                case TOUCH_TYPE_BUTTON:
                    if (control.touchID && control.buttonID != CHAT_BUTTON && control.buttonID != PLAYERLIST_BUTTON && control.buttonID != CONSOLE_BUTTON) {
                        pad->button |= control.buttonID;
                    }
                    break;
                case TOUCH_TYPE_PAD:
                    break;
            }
        }
    }
}

static u32 touchscreen_rawkey(void) { 
    return VK_INVALID;
}

static void touchscreen_bind(void) {
}

static void touchscreen_shutdown(void) {
}

struct ControllerAPI controller_touchscreen = {
    VK_BASE_TOUCHSCREEN,
    touchscreen_init,
    touchscreen_read,
    touchscreen_rawkey,
    NULL,
    NULL,
    touchscreen_bind,
    touchscreen_shutdown
};
#endif
