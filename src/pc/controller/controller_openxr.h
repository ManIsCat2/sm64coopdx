#ifndef CONTROLLER_OPENXR_H
#define CONTROLLER_OPENXR_H

#include "controller_api.h"

// Virtual key base for OpenXR controller
#define VK_BASE_OPENXR 0x2000

// OpenXR controller button virtual keys
#define VK_OPENXR_A           (VK_BASE_OPENXR + 0)
#define VK_OPENXR_B           (VK_BASE_OPENXR + 1)
#define VK_OPENXR_X           (VK_BASE_OPENXR + 2)
#define VK_OPENXR_Y           (VK_BASE_OPENXR + 3)
#define VK_OPENXR_MENU        (VK_BASE_OPENXR + 4)
#define VK_OPENXR_L_TRIGGER   (VK_BASE_OPENXR + 5)
#define VK_OPENXR_R_TRIGGER   (VK_BASE_OPENXR + 6)
#define VK_OPENXR_L_SQUEEZE   (VK_BASE_OPENXR + 7)
#define VK_OPENXR_R_SQUEEZE   (VK_BASE_OPENXR + 8)
#define VK_OPENXR_L_STICK_UP    (VK_BASE_OPENXR + 9)
#define VK_OPENXR_L_STICK_DOWN  (VK_BASE_OPENXR + 10)
#define VK_OPENXR_L_STICK_LEFT  (VK_BASE_OPENXR + 11)
#define VK_OPENXR_L_STICK_RIGHT (VK_BASE_OPENXR + 12)
#define VK_OPENXR_R_STICK_UP    (VK_BASE_OPENXR + 13)
#define VK_OPENXR_R_STICK_DOWN  (VK_BASE_OPENXR + 14)
#define VK_OPENXR_R_STICK_LEFT  (VK_BASE_OPENXR + 15)
#define VK_OPENXR_R_STICK_RIGHT (VK_BASE_OPENXR + 16)
#define VK_OPENXR_L_STICK_CLICK (VK_BASE_OPENXR + 17)
#define VK_OPENXR_R_STICK_CLICK (VK_BASE_OPENXR + 18)

#ifdef __cplusplus
extern "C" {
#endif

extern struct ControllerAPI controller_openxr;

#ifdef __cplusplus
}
#endif

#ifdef OPENXR_ENABLED
// C++ only functions
#include <openxr/openxr.h>
#include <stdbool.h>
XrSpace controller_openxr_get_keyboard_space(void);
XrSpace controller_openxr_get_left_hand_space(void);
bool controller_openxr_get_left_hand_palm_pose(XrPosef* out_pose);
#endif

#endif
