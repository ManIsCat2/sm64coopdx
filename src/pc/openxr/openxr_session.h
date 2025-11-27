#ifndef OPENXR_SESSION_H
#define OPENXR_SESSION_H

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

// Session management
XrSession createXRSession(
    XrInstance instance,
    XrSystemId systemID,
    VkInstance vulkanInstance,
    VkPhysicalDevice physDevice,
    VkDevice device,
    uint32_t queueFamilyIndex
);
void destroyXRSession(XrSession session);

// Space management
XrSpace createXRSpace(XrSession session);
void destroyXRSpace(XrSpace space);

#ifdef __cplusplus
}
#endif

#endif // OPENXR_SESSION_H

