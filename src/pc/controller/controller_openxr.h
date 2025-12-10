#ifndef CONTROLLER_OPENXR_H
#define CONTROLLER_OPENXR_H

#include "controller_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct ControllerAPI controller_openxr;

#ifdef __cplusplus
}

// C++ only functions
#include <openxr/openxr.h>
XrSpace controller_openxr_get_keyboard_space(void);
#endif

#endif
