#ifndef VR_COPY_H
#define VR_COPY_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize VR copy system (OpenGL-Vulkan interop)
// This creates shared memory objects for efficient frame transfer
// Returns 1 on success, 0 on failure
int vr_copy_init(void);

// Shutdown VR copy system
void vr_copy_shutdown(void);

// Check if VR copy is initialized
int vr_copy_is_initialized(void);

// Copy rendered framebuffer to Vulkan swapchain image
// eye: 0 for left, 1 for right
// Returns 1 on success, 0 on failure
int vr_copy_framebuffer_to_swapchain(int eye);

#ifdef __cplusplus
}
#endif

#endif // VR_COPY_H

