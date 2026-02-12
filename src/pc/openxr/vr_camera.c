#include "vr_camera.h"
#include "openxr_manager.h"
#include "game/camera.h"
#include "game/level_update.h"
#include "game/area.h"

#include <math.h>
#include <stdio.h>

static int vr_camera_initialized = 0;
static int vr_camera_active = 0;
static s16 vr_camera_yaw_offset = 0;  // Joystick-controlled yaw offset

// Camera state
static struct {
    float yaw;
    float pitch;
    float roll;
    float x;
    float y;
    float z;
} vr_camera_state = {0};

void vr_camera_init(void)
{
    vr_camera_initialized = 1;
    vr_camera_active = openxr_is_initialized();
}

void vr_camera_update(void)
{
    if (!vr_camera_initialized || !openxr_is_initialized()) {
        vr_camera_active = 0;
        return;
    }

    // Get head rotation from OpenXR
    if (!openxr_get_head_rotation(&vr_camera_state.yaw, &vr_camera_state.pitch, &vr_camera_state.roll)) {
        vr_camera_active = 0;
        return;
    }

    // Get head position from OpenXR
    if (!openxr_get_head_position(&vr_camera_state.x, &vr_camera_state.y, &vr_camera_state.z)) {
        vr_camera_active = 0;
        return;
    }

    vr_camera_active = 1;
}

void vr_camera_apply_to_lakitu(struct Camera *c)
{
    if (!vr_camera_active || !c) {
        return;
    }

    // Apply VR head rotation to Lakitu camera
    // SM64 uses a different coordinate system, so we need to convert:
    // OpenXR: +Y up, +X right, -Z forward
    // SM64: +Y up, +X right, +Z forward (with rotation in degrees * 182.044444)
    
    // Convert yaw from degrees to SM64 angle format
    // SM64 uses 65536 units per full rotation (360 degrees = 65536 units)
    // 1 degree = 182.044444 units
    float sm64_yaw = -vr_camera_state.yaw * 182.044444f;
    
    // Set camera yaw based on VR headset rotation
    c->yaw = (s16)sm64_yaw;
    
    // Also update gLakituState which is used for rendering
    gLakituState.yaw = (s16)sm64_yaw;
    
    // Update focus pitch for looking up/down
    // Lakitu tilt is limited in the original game, but we can update focus position
    if (c->mode != CAMERA_MODE_NEWCAM) {
        // Calculate pitch offset for focus point
        float pitch_radians = vr_camera_state.pitch * (M_PI / 180.0f);
        float distance = 400.0f; // Distance from camera to focus point
        
        // Adjust focus height based on pitch
        float height_offset = distance * sinf(pitch_radians);
        
        // Store original focus Y
        static float base_focus_y = 0;
        if (base_focus_y == 0) {
            base_focus_y = c->focus[1];
        }
        
        // Apply pitch to focus point
        c->focus[1] = base_focus_y + height_offset;
        gLakituState.focus[1] = c->focus[1];
    }
}

void vr_camera_apply_to_default_camera(struct Camera *c, s16 *yaw)
{
    if (!vr_camera_active || !c || !yaw) {
        return;
    }

    // Apply VR head rotation to default camera mode
    // In default camera mode, the yaw is returned and used by the caller
    // We need to override it with VR rotation
    
    // Convert yaw from degrees to SM64 angle format
    float sm64_yaw = -vr_camera_state.yaw * 182.044444f;
    
    // Override the calculated yaw with VR yaw
    *yaw = (s16)sm64_yaw;
    
    // Also set c->yaw directly
    c->yaw = (s16)sm64_yaw;
    
    // Update focus pitch for looking up/down (same as in Lakitu mode)
    if (c->mode != CAMERA_MODE_NEWCAM) {
        // Calculate pitch offset for focus point
        float pitch_radians = vr_camera_state.pitch * (M_PI / 180.0f);
        float distance = 400.0f; // Distance from camera to focus point
        
        // Adjust focus height based on pitch
        float height_offset = distance * sinf(pitch_radians);
        
        // Store original focus Y
        static float base_focus_y = 0;
        if (base_focus_y == 0) {
            base_focus_y = c->focus[1];
        }
        
        // Apply pitch to focus point
        c->focus[1] = base_focus_y + height_offset;
    }
}

int vr_camera_is_active(void)
{
    return vr_camera_active;
}

s16 vr_camera_get_yaw(void)
{
    if (!vr_camera_active) {
        return 0;
    }
    
    // Get head quaternion
    float qx, qy, qz, qw;
    if (!openxr_get_head_quaternion(&qx, &qy, &qz, &qw)) {
        return 0;
    }
    
    // Calculate forward vector from quaternion
    // This is the forward direction the head is facing
    float forwardX = 2.0f * (qx * qz + qy * qw);
    float forwardY = 2.0f * (qy * qz - qx * qw);
    float forwardZ = 1.0f - 2.0f * (qx * qx + qy * qy);
    
    // Project to horizontal plane (ignore Y component for yaw-only rotation)
    float horizX = forwardX;
    float horizZ = forwardZ;
    
    // Normalize the horizontal vector
    float horizLen = sqrtf(horizX * horizX + horizZ * horizZ);
    if (horizLen > 0.0001f) {
        horizX /= horizLen;
        horizZ /= horizLen;
    }
    
    // Calculate yaw angle from horizontal forward vector
    // This is the Position Angle (Backwards) in SM64 coords.
    // horizX, horizZ points Back (Z+).
    // atan2f(horizX, horizZ) gives CCW angle from South.
        float yaw = -(atan2f(horizX, horizZ));// - M_PI / 2.0f);

    // float yaw = atan2f(horizX, horizZ);
    
    // Convert from radians to SM64 angle format
    // SM64 uses 65536 units per full rotation (360 degrees = 2*PI radians = 65536 units)
    // So 1 radian = 65536 / (2*PI) = 10430.378 units
    // No negation is needed as atan2f is CCW and SM64 is CCW.
    float sm64_yaw = yaw * (65536.0f / (2.0f * M_PI));
    
    // Add 180 degrees (0x8000) offset to face the correct direction
    return (s16)sm64_yaw + vr_camera_yaw_offset;
}


void vr_camera_add_yaw_offset(s16 delta)
{
    vr_camera_yaw_offset += delta;
}

void vr_camera_reset_yaw_offset(s16 targetYaw)
{
    if (!vr_camera_active) {
        return;
    }

    // Get the raw yaw from the headset (without offset)
    s16 current_vr_yaw = vr_camera_get_yaw() - vr_camera_yaw_offset;
    
    // We want: current_vr_yaw + new_offset = targetYaw
    // So: new_offset = targetYaw - current_vr_yaw
    vr_camera_yaw_offset = targetYaw - current_vr_yaw;
}

s16 vr_camera_get_yaw_delta(void)
{
    if (!vr_camera_active) {
        return 0;
    }
    
    // Get head quaternion
    float qx, qy, qz, qw;
    if (!openxr_get_head_quaternion(&qx, &qy, &qz, &qw)) {
        return 0;
    }
    
    // Calculate forward vector from quaternion
    float forwardX = 2.0f * (qx * qz + qy * qw);
    float forwardY = 2.0f * (qy * qz - qx * qw);
    float forwardZ = 1.0f - 2.0f * (qx * qx + qy * qy);
    
    // Project to horizontal plane (ignore Y component for yaw-only rotation)
    float horizX = forwardX;
    float horizZ = forwardZ;
    
    // Normalize the horizontal vector
    float horizLen = sqrtf(horizX * horizX + horizZ * horizZ);
    if (horizLen > 0.0001f) {
        horizX /= horizLen;
        horizZ /= horizLen;
    }
    
    // Calculate yaw angle from horizontal forward vector
    float yaw = atan2f(horizX, horizZ);
    
    // Convert from radians to SM64 angle format
    float sm64_yaw = yaw * (65536.0f / (2.0f * M_PI));
    
    // Return raw yaw without offset
    return (s16)sm64_yaw;
}


s16 vr_camera_get_pitch(void)
{
    if (!vr_camera_active) {
        return 0;
    }
    
    // Get head quaternion
    float qx, qy, qz, qw;
    if (!openxr_get_head_quaternion(&qx, &qy, &qz, &qw)) {
        return 0;
    }
    
    // Calculate pitch from quaternion
    // Pitch is rotation around X axis
    // Use the formula: pitch = asin(2 * (qw * qx - qy * qz))
    float sinPitch = 2.0f * (qw * qx - qy * qz);
    
    // Clamp to avoid asin domain errors
    if (sinPitch > 1.0f) sinPitch = 1.0f;
    if (sinPitch < -1.0f) sinPitch = -1.0f;
    
    float pitch = asinf(sinPitch);
    
    // Convert from radians to SM64 angle format
    // Negate because SM64 pitch is inverted (positive = looking down)
    float sm64_pitch = -pitch * (65536.0f / (2.0f * M_PI));
    
    return (s16)sm64_pitch;
}
