#include "vr_camera.h"
#include "openxr_manager.h"
#include "game/camera.h"
#include "game/level_update.h"
#include "game/area.h"

#include <math.h>
#include <stdio.h>

static int vr_camera_initialized = 0;
static int vr_camera_active = 0;

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

    // Debug logging - print VR headset data
    static int log_counter = 0;
    if (log_counter % 60 == 0) {  // Log once per second (at 60 fps)
        printf("\n=== VR HEADSET DATA ===\n");
        printf("Rotation: Yaw=%.2f° Pitch=%.2f° Roll=%.2f°\n", 
               vr_camera_state.yaw, vr_camera_state.pitch, vr_camera_state.roll);
        printf("Position: X=%.3fm Y=%.3fm Z=%.3fm\n", 
               vr_camera_state.x, vr_camera_state.y, vr_camera_state.z);
    }
    
    log_counter++;
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
    
    // Debug logging - print Lakitu camera data
    static int log_counter = 0;
    if (log_counter % 60 == 0) {  // Log once per second
        printf("\n=== LAKITU CAMERA DATA ===\n");
        printf("Camera Yaw: %d (%.2f°)\n", c->yaw, c->yaw / 182.044444f);
        printf("Camera Position: X=%.1f Y=%.1f Z=%.1f\n", 
               c->pos[0], c->pos[1], c->pos[2]);
        printf("Camera Focus: X=%.1f Y=%.1f Z=%.1f\n", 
               c->focus[0], c->focus[1], c->focus[2]);
        printf("Lakitu Position: X=%.1f Y=%.1f Z=%.1f\n", 
               gLakituState.pos[0], gLakituState.pos[1], gLakituState.pos[2]);
        printf("Lakitu Focus: X=%.1f Y=%.1f Z=%.1f\n", 
               gLakituState.focus[0], gLakituState.focus[1], gLakituState.focus[2]);
        printf("Lakitu Yaw: %d (%.2f°)\n", gLakituState.yaw, gLakituState.yaw / 182.044444f);
        printf("==========================\n\n");
    }
    log_counter++;
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

