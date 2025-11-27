#ifndef OPENXR_INSTANCE_H
#define OPENXR_INSTANCE_H

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

// Get OpenXR extension function
PFN_xrVoidFunction getXRFunction(XrInstance instance, const char* name);

// OpenXR error callback
XrBool32 handleXRError(
    XrDebugUtilsMessageSeverityFlagsEXT severity,
    XrDebugUtilsMessageTypeFlagsEXT type,
    const XrDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData
);

// Instance management
XrInstance createXRInstance();
void destroyXRInstance(XrInstance instance);

// Debug messenger
XrDebugUtilsMessengerEXT createXRDebugMessenger(XrInstance instance);
void destroyXRDebugMessenger(XrInstance instance, XrDebugUtilsMessengerEXT debugMessenger);

// System
XrSystemId getXRSystem(XrInstance instance);

#ifdef __cplusplus
}
#endif

#endif // OPENXR_INSTANCE_H

