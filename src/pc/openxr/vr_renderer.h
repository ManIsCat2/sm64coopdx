#ifndef VR_RENDERER_H
#define VR_RENDERER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize VR renderer
// Must be called after OpenXR session is created
// Returns 1 on success, 0 on failure
int vr_renderer_init(void);

// Shutdown VR renderer
void vr_renderer_shutdown(void);

// Check if VR renderer is initialized
int vr_renderer_is_initialized(void);

// Begin VR frame rendering
// Call this before rendering the scene
// Returns 1 if should render, 0 if should skip this frame
int vr_renderer_begin_frame(void);

// Render to a specific eye
// eye: 0 for left, 1 for right
// Returns 1 on success, 0 on failure
int vr_renderer_render_eye(int eye);

// End VR frame rendering
// Submits the rendered images to OpenXR
// Call this after rendering both eyes
int vr_renderer_end_frame(void);

// Get view matrices for left and right eyes
// Returns 1 on success, 0 on failure
int vr_renderer_get_view_matrix(int eye, float* matrix);

// Get projection matrices for left and right eyes
// Returns 1 on success, 0 on failure
int vr_renderer_get_projection_matrix(int eye, float* matrix);

// Get the viewport dimensions for an eye
void vr_renderer_get_viewport(int eye, uint32_t* width, uint32_t* height);

// Get per-eye pose and FOV (for stereo rendering)
// Returns 1 on success, 0 on failure
// position: x, y, z in meters
// orientation: quaternion (x, y, z, w)
int vr_renderer_get_eye_pose(int eye, float* position_x, float* position_y, float* position_z,
                               float* orientation_x, float* orientation_y, float* orientation_z, float* orientation_w);

#ifdef __cplusplus
}

// C++ only - Get Vulkan handles for interop
#include <vulkan/vulkan.h>

// Get the current swapchain image for an eye
// Returns VK_NULL_HANDLE if not available
VkImage vr_renderer_get_swapchain_image(int eye);

// Get the swapchain image format
// Returns 0 if not initialized
uint32_t vr_renderer_get_swapchain_format(int eye);

// Get the number of swapchain images per eye
uint32_t vr_renderer_get_swapchain_image_count(int eye);
#endif

#endif // VR_RENDERER_H

