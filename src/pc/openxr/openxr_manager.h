#ifndef OPENXR_MANAGER_H
#define OPENXR_MANAGER_H

#include <stdint.h>


#include "openxr_platform_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize OpenXR context
int openxr_init(void);

// Shutdown OpenXR context
void openxr_shutdown(void);

// Check if OpenXR is initialized
int openxr_is_initialized(void);

// Update OpenXR (poll events, wait for frame)
// Should be called before rendering
// Returns 1 if VR rendering should happen, 0 otherwise
int openxr_update(void);

// Recenter the VR view
void openxr_recenter_view(void);

// Get head rotation (in degrees)
// Returns 1 on success, 0 if not available
int openxr_get_head_rotation(float* yaw, float* pitch, float* roll);

// Get head position (in meters)
// Returns 1 on success, 0 if not available
int openxr_get_head_position(float* x, float* y, float* z);

// Get head orientation as quaternion
// Returns 1 on success, 0 if not available
int openxr_get_head_quaternion(float* x, float* y, float* z, float* w);

// Get predicted display time for current frame
int64_t openxr_get_predicted_display_time(void);

// Get OpenXR handles for VR renderer
XrInstance openxr_get_instance(void);
XrSession openxr_get_session(void);
XrSpace openxr_get_space(void);
XrSystemId openxr_get_system_id(void);



// End frame with no layers (used when VR rendering didn't happen but xrBeginFrame was called)
int openxr_end_frame_empty(void);

#ifdef __cplusplus
}
#endif

#endif // OPENXR_MANAGER_H
