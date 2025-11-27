#ifndef OPENXR_MANAGER_H
#define OPENXR_MANAGER_H

#ifdef __cplusplus
// Include OpenXR types for C++ functions
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

extern "C" {
#endif

// Initialize OpenXR context with Vulkan backend
// Returns 1 on success, 0 on failure
// Failures are non-fatal - game can continue without VR
int openxr_init(void);

// Shutdown OpenXR context
void openxr_shutdown(void);

// Check if OpenXR is initialized
int openxr_is_initialized(void);

// Update OpenXR state (call once per frame)
// Returns 1 if successful, 0 if not initialized or session not running
int openxr_update(void);

// Get head rotation in degrees
// yaw: rotation around Y axis (left/right) in degrees
// pitch: rotation around X axis (up/down) in degrees
// roll: rotation around Z axis (tilt) in degrees
// Returns 1 if successful, 0 if not initialized
int openxr_get_head_rotation(float* yaw, float* pitch, float* roll);

// Get head position in meters
// x, y, z: position in meters
// Returns 1 if successful, 0 if not initialized
int openxr_get_head_position(float* x, float* y, float* z);

#ifdef __cplusplus
}

// Get OpenXR handles for VR renderer (C++ only)
// These functions expose internal handles to the VR renderer
XrInstance openxr_get_instance(void);
XrSession openxr_get_session(void);
XrSpace openxr_get_space(void);
XrSystemId openxr_get_system_id(void);

// Get Vulkan handles for VR interop (C++ only)
VkInstance openxr_get_vulkan_instance(void);
VkPhysicalDevice openxr_get_vulkan_physical_device(void);
VkDevice openxr_get_vulkan_device(void);
VkQueue openxr_get_vulkan_queue(void);
int32_t openxr_get_vulkan_queue_family_index(void);
#endif

#endif // OPENXR_MANAGER_H

