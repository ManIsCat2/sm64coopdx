#ifndef VULKAN_INSTANCE_H
#define VULKAN_INSTANCE_H

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#ifdef __cplusplus
#include <set>
#include <string>

extern "C" {
#endif

// Get Vulkan extension function
PFN_vkVoidFunction getVKFunction(VkInstance instance, const char* name);

// Vulkan error callback
VkBool32 handleVKError(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData
);

#ifdef __cplusplus
}

// Get Vulkan requirements from OpenXR (C++ only for std::set)
std::tuple<XrGraphicsRequirementsVulkanKHR, std::set<std::string>> getVulkanInstanceRequirements(XrInstance instance, XrSystemId system);

// Instance management
VkInstance createVulkanInstance(XrGraphicsRequirementsVulkanKHR graphicsRequirements, std::set<std::string> instanceExtensions);
#else
// C-compatible wrapper
VkInstance createVulkanInstanceSimple(XrInstance xrInstance, XrSystemId system);
#endif

void destroyVulkanInstance(VkInstance instance);

// Debug messenger
VkDebugUtilsMessengerEXT createVulkanDebugMessenger(VkInstance instance);
void destroyVulkanDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger);

#endif // VULKAN_INSTANCE_H

