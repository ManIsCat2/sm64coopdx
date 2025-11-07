#ifndef VULKAN_DEVICE_H
#define VULKAN_DEVICE_H

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#ifdef __cplusplus
#include <set>
#include <string>
#include <tuple>

// Get Vulkan device requirements from OpenXR (C++ only for std::set)
std::tuple<VkPhysicalDevice, std::set<std::string>> getVulkanDeviceRequirements(XrInstance instance, XrSystemId system, VkInstance vulkanInstance);

// Device queue family
int32_t getDeviceQueueFamily(VkPhysicalDevice physicalDevice);

// Device management
std::tuple<VkDevice, VkQueue> createVulkanDevice(
    VkPhysicalDevice physicalDevice,
    int32_t graphicsQueueFamilyIndex,
    std::set<std::string> deviceExtensions
);

extern "C" {
#endif

void destroyVulkanDevice(VkDevice device);

#ifdef __cplusplus
}
#endif

#endif // VULKAN_DEVICE_H

