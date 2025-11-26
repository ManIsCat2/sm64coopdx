#ifdef TOUCH_CONTROLS
#include "controller_touchscreen_textures.h"

// Joystick
ALIGNED8 const u8 texture_touch_joystick[] = {
#include "textures/touchcontrols/touch_joystick.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_joystick_base[] = {
#include "textures/touchcontrols/touch_joystick_base.rgba16.inc.c"
};

// C Buttons
ALIGNED8 const u8 texture_touch_c_up[] = {
#include "textures/touchcontrols/touch_button_c_up.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_c_up_pressed[] = {
#include "textures/touchcontrols/touch_button_c_up_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_c_down[] = {
#include "textures/touchcontrols/touch_button_c_down.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_c_down_pressed[] = {
#include "textures/touchcontrols/touch_button_c_down_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_c_left[] = {
#include "textures/touchcontrols/touch_button_c_left.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_c_left_pressed[] = {
#include "textures/touchcontrols/touch_button_c_left_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_c_right[] = {
#include "textures/touchcontrols/touch_button_c_right.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_c_right_pressed[] = {
#include "textures/touchcontrols/touch_button_c_right_pressed.rgba16.inc.c"
};

// D-Pad Buttons
ALIGNED8 const u8 texture_touch_dpad_up[] = {
#include "textures/touchcontrols/touch_button_dpad_up.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_dpad_up_pressed[] = {
#include "textures/touchcontrols/touch_button_dpad_up_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_dpad_down[] = {
#include "textures/touchcontrols/touch_button_dpad_down.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_dpad_down_pressed[] = {
#include "textures/touchcontrols/touch_button_dpad_down_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_dpad_left[] = {
#include "textures/touchcontrols/touch_button_dpad_left.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_dpad_left_pressed[] = {
#include "textures/touchcontrols/touch_button_dpad_left_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_dpad_right[] = {
#include "textures/touchcontrols/touch_button_dpad_right.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_dpad_right_pressed[] = {
#include "textures/touchcontrols/touch_button_dpad_right_pressed.rgba16.inc.c"
};

// Normal Buttons
ALIGNED8 const u8 texture_touch_a[] = {
#include "textures/touchcontrols/touch_button_a.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_a_pressed[] = {
#include "textures/touchcontrols/touch_button_a_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_b[] = {
#include "textures/touchcontrols/touch_button_b.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_b_pressed[] = {
#include "textures/touchcontrols/touch_button_b_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_x[] = {
#include "textures/touchcontrols/touch_button_x.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_x_pressed[] = {
#include "textures/touchcontrols/touch_button_x_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_y[] = {
#include "textures/touchcontrols/touch_button_y.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_y_pressed[] = {
#include "textures/touchcontrols/touch_button_y_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_start[] = {
#include "textures/touchcontrols/touch_button_start.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_start_pressed[] = {
#include "textures/touchcontrols/touch_button_start_pressed.rgba16.inc.c"
};

// Trigger Buttons
ALIGNED8 const u8 texture_touch_l[] = {
#include "textures/touchcontrols/touch_button_l.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_l_pressed[] = {
#include "textures/touchcontrols/touch_button_l_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_r[] = {
#include "textures/touchcontrols/touch_button_r.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_r_pressed[] = {
#include "textures/touchcontrols/touch_button_r_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_z[] = {
#include "textures/touchcontrols/touch_button_z.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_z_pressed[] = {
#include "textures/touchcontrols/touch_button_z_pressed.rgba16.inc.c"
};

// Misc Buttons
ALIGNED8 const u8 texture_touch_chat[] = {
#include "textures/touchcontrols/touch_button_chat.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_chat_pressed[] = {
#include "textures/touchcontrols/touch_button_chat_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_playerlist[] = {
#include "textures/touchcontrols/touch_button_playerlist.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_playerlist_pressed[] = {
#include "textures/touchcontrols/touch_button_playerlist_pressed.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_console[] = {
#include "textures/touchcontrols/touch_button_console.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_console_pressed[] = {
#include "textures/touchcontrols/touch_button_console_pressed.rgba16.inc.c"
};

const Texture *const touch_textures[] = {
    texture_touch_joystick,
    texture_touch_joystick_base,
    texture_touch_c_up,
    texture_touch_c_up_pressed,
    texture_touch_c_down,
    texture_touch_c_down_pressed,
    texture_touch_c_left,
    texture_touch_c_left_pressed,
    texture_touch_c_right,
    texture_touch_c_right_pressed,
    texture_touch_dpad_up,
    texture_touch_dpad_up_pressed,
    texture_touch_dpad_down,
    texture_touch_dpad_down_pressed,
    texture_touch_dpad_left,
    texture_touch_dpad_left_pressed,
    texture_touch_dpad_right,
    texture_touch_dpad_right_pressed,
    texture_touch_a,
    texture_touch_a_pressed,
    texture_touch_b,
    texture_touch_b_pressed,
    texture_touch_x,
    texture_touch_x_pressed,
    texture_touch_y,
    texture_touch_y_pressed,
    texture_touch_start,
    texture_touch_start_pressed,
    texture_touch_l,
    texture_touch_l_pressed,
    texture_touch_r,
    texture_touch_r_pressed,
    texture_touch_z,
    texture_touch_z_pressed,
    texture_touch_chat,
    texture_touch_chat_pressed,
    texture_touch_playerlist,
    texture_touch_playerlist_pressed,
    texture_touch_console,
    texture_touch_console_pressed,
}; 
#endif