#ifndef VR_CAMERA_H
#define VR_CAMERA_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Camera;

// Initialize VR camera system
void vr_camera_init(void);

// Update camera with VR head tracking
// Should be called each frame before camera updates
void vr_camera_update(void);

// Apply VR rotation to the camera
// Called from within the camera update loop in camera.c
void vr_camera_apply_to_lakitu(struct Camera *c);

// Apply VR rotation to the default camera mode
// Called from within update_default_camera() in camera.c
void vr_camera_apply_to_default_camera(struct Camera *c, s16 *yaw);

// Check if VR camera is active
int vr_camera_is_active(void);

#ifdef __cplusplus
}
#endif

#endif // VR_CAMERA_H

