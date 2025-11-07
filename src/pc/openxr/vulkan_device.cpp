#include "vulkan_device.h"
#include "openxr_instance.h"
#include <iostream>
#include <vector>
#include <set>
#include <string>

using namespace std;

tuple<VkPhysicalDevice, set<string>> getVulkanDeviceRequirements(XrInstance instance, XrSystemId system, VkInstance vulkanInstance)
{
    auto xrGetVulkanGraphicsDeviceKHR = (PFN_xrGetVulkanGraphicsDeviceKHR)getXRFunction(instance, "xrGetVulkanGraphicsDeviceKHR");
    auto xrGetVulkanDeviceExtensionsKHR = (PFN_xrGetVulkanDeviceExtensionsKHR)getXRFunction(instance, "xrGetVulkanDeviceExtensionsKHR");

    if (!xrGetVulkanGraphicsDeviceKHR || !xrGetVulkanDeviceExtensionsKHR) {
        cerr << "Failed to get OpenXR Vulkan device functions" << endl;
        return { VK_NULL_HANDLE, {} };
    }

    VkPhysicalDevice physicalDevice;

    XrResult result = xrGetVulkanGraphicsDeviceKHR(instance, system, vulkanInstance, &physicalDevice);

    if (result != XR_SUCCESS)
    {
        cerr << "Failed to get Vulkan graphics device: " << result << endl;
        return { VK_NULL_HANDLE, {} };
    }

    uint32_t deviceExtensionsSize;

    result = xrGetVulkanDeviceExtensionsKHR(instance, system, 0, &deviceExtensionsSize, nullptr);

    if (result != XR_SUCCESS)
    {
        cerr << "Failed to get Vulkan device extensions: " << result << endl;
        return { VK_NULL_HANDLE, {} };
    }

    char* deviceExtensionsData = new char[deviceExtensionsSize];

    result = xrGetVulkanDeviceExtensionsKHR(instance, system, deviceExtensionsSize, &deviceExtensionsSize, deviceExtensionsData);

    if (result != XR_SUCCESS)
    {
        cerr << "Failed to get Vulkan device extensions: " << result << endl;
        delete[] deviceExtensionsData;
        return { VK_NULL_HANDLE, {} };
    }

    set<string> deviceExtensions;

    uint32_t last = 0;
    for (uint32_t i = 0; i <= deviceExtensionsSize; i++)
    {
        if (i == deviceExtensionsSize || deviceExtensionsData[i] == ' ')
        {
            deviceExtensions.insert(string(deviceExtensionsData + last, i - last));
            last = i + 1;
        }
    }

    delete[] deviceExtensionsData;

    return { physicalDevice, deviceExtensions };
}

int32_t getDeviceQueueFamily(VkPhysicalDevice physicalDevice)
{
    int32_t graphicsQueueFamilyIndex = -1;

    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    for (int32_t i = 0; i < (int32_t)queueFamilyCount; i++)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            graphicsQueueFamilyIndex = i;
            break;
        }
    }

    if (graphicsQueueFamilyIndex == -1)
    {
        cerr << "No graphics queue found." << endl;
        return graphicsQueueFamilyIndex;
    }

    return graphicsQueueFamilyIndex;
}

tuple<VkDevice, VkQueue> createVulkanDevice(
    VkPhysicalDevice physicalDevice,
    int32_t graphicsQueueFamilyIndex,
    set<string> deviceExtensions
)
{
    VkDevice device;

    size_t extensionCount = deviceExtensions.size();
    const char** extensions = new const char*[extensionCount];

    size_t i = 0;
    for (const string& deviceExtension : deviceExtensions)
    {
        extensions[i] = deviceExtension.c_str();
        i++;
    }

    float priority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;

    VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);

    delete[] extensions;

    if (result != VK_SUCCESS)
    {
        cerr << "Failed to create Vulkan device: " << result << endl;
        return { VK_NULL_HANDLE, VK_NULL_HANDLE };
    }

    VkQueue queue;
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &queue);

    cout << "Vulkan device created successfully" << endl;

    return { device, queue };
}

void destroyVulkanDevice(VkDevice device)
{
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
}

