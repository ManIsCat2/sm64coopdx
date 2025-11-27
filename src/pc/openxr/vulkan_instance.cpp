#include "vulkan_instance.h"
#include "openxr_instance.h"
#include <iostream>
#include <set>
#include <string>
#include <tuple>

using namespace std;

// Application information
static const char* const applicationName = "sm64coopdx";
static const unsigned int majorVersion = 1;
static const unsigned int minorVersion = 0;
static const unsigned int patchVersion = 0;

static const char* const vulkanLayerNames[] = { "VK_LAYER_KHRONOS_validation" };
static const char* const vulkanExtensionNames[] = { "VK_EXT_debug_utils" };

PFN_vkVoidFunction getVKFunction(VkInstance instance, const char* name)
{
    PFN_vkVoidFunction func = vkGetInstanceProcAddr(instance, name);

    if (!func)
    {
        cerr << "Failed to load Vulkan extension function '" << name << "'." << endl;
        return nullptr;
    }

    return func;
}

VkBool32 handleVKError(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData
)
{
    cerr << "Vulkan ";

    switch (type)
    {
    case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT :
        cerr << "general ";
        break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT :
        cerr << "validation ";
        break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT :
        cerr << "performance ";
        break;
    }

    switch (severity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT :
        cerr << "(verbose): ";
        break;
    default :
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT :
        cerr << "(info): ";
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT :
        cerr << "(warning): ";
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT :
        cerr << "(error): ";
        break;
    }

    cerr << callbackData->pMessage << endl;

    return VK_FALSE;
}

tuple<XrGraphicsRequirementsVulkanKHR, set<string>> getVulkanInstanceRequirements(XrInstance instance, XrSystemId system)
{
    auto xrGetVulkanGraphicsRequirementsKHR = (PFN_xrGetVulkanGraphicsRequirementsKHR)getXRFunction(instance, "xrGetVulkanGraphicsRequirementsKHR");
    auto xrGetVulkanInstanceExtensionsKHR = (PFN_xrGetVulkanInstanceExtensionsKHR)getXRFunction(instance, "xrGetVulkanInstanceExtensionsKHR");

    if (!xrGetVulkanGraphicsRequirementsKHR || !xrGetVulkanInstanceExtensionsKHR) {
        cerr << "Failed to get OpenXR Vulkan functions" << endl;
        return { {}, {} };
    }

    XrGraphicsRequirementsVulkanKHR graphicsRequirements{};
    graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;

    XrResult result = xrGetVulkanGraphicsRequirementsKHR(instance, system, &graphicsRequirements);

    if (result != XR_SUCCESS)
    {
        cerr << "Failed to get Vulkan graphics requirements: " << result << endl;
        return { graphicsRequirements, {} };
    }

    uint32_t instanceExtensionsSize;

    result = xrGetVulkanInstanceExtensionsKHR(instance, system, 0, &instanceExtensionsSize, nullptr);

    if (result != XR_SUCCESS)
    {
        cerr << "Failed to get Vulkan instance extensions: " << result << endl;
        return { graphicsRequirements, {} };
    }

    char* instanceExtensionsData = new char[instanceExtensionsSize];

    result = xrGetVulkanInstanceExtensionsKHR(instance, system, instanceExtensionsSize, &instanceExtensionsSize, instanceExtensionsData);

    if (result != XR_SUCCESS)
    {
        cerr << "Failed to get Vulkan instance extensions: " << result << endl;
        delete[] instanceExtensionsData;
        return { graphicsRequirements, {} };
    }

    set<string> instanceExtensions;

    uint32_t last = 0;
    for (uint32_t i = 0; i <= instanceExtensionsSize; i++)
    {
        if (i == instanceExtensionsSize || instanceExtensionsData[i] == ' ')
        {
            instanceExtensions.insert(string(instanceExtensionsData + last, i - last));
            last = i + 1;
        }
    }

    delete[] instanceExtensionsData;

    return { graphicsRequirements, instanceExtensions };
}

VkInstance createVulkanInstance(XrGraphicsRequirementsVulkanKHR graphicsRequirements, set<string> instanceExtensions)
{
    VkInstance instance;

    size_t extensionCount = 1 + instanceExtensions.size();
    const char** extensionNames = new const char*[extensionCount];

    size_t i = 0;
    extensionNames[i] = vulkanExtensionNames[0];
    i++;

    for (const string& instanceExtension : instanceExtensions)
    {
        extensionNames[i] = instanceExtension.c_str();
        i++;
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = applicationName;
    applicationInfo.applicationVersion = VK_MAKE_VERSION(majorVersion, minorVersion, patchVersion);
    applicationInfo.pEngineName = applicationName;
    applicationInfo.engineVersion = VK_MAKE_VERSION(majorVersion, minorVersion, patchVersion);
    applicationInfo.apiVersion = VK_MAKE_API_VERSION(
        0,
        XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported),
        XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported),
        0
    );

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensionNames;
    createInfo.enabledLayerCount = 0;  // Disable validation layers by default
    createInfo.ppEnabledLayerNames = nullptr;

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);

    delete[] extensionNames;

    if (result != VK_SUCCESS)
    {
        cerr << "Failed to create Vulkan instance: " << result << endl;
        return VK_NULL_HANDLE;
    }

    cout << "Vulkan instance created successfully" << endl;

    return instance;
}

void destroyVulkanInstance(VkInstance instance)
{
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
}

VkDebugUtilsMessengerEXT createVulkanDebugMessenger(VkInstance instance)
{
    VkDebugUtilsMessengerEXT debugMessenger;

    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo{};
    debugMessengerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugMessengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugMessengerCreateInfo.pfnUserCallback = handleVKError;

    auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)getVKFunction(instance, "vkCreateDebugUtilsMessengerEXT");

    if (!vkCreateDebugUtilsMessengerEXT) {
        cerr << "Failed to get vkCreateDebugUtilsMessengerEXT function" << endl;
        return VK_NULL_HANDLE;
    }

    VkResult result = vkCreateDebugUtilsMessengerEXT(instance, &debugMessengerCreateInfo, nullptr, &debugMessenger);

    if (result != VK_SUCCESS)
    {
        cerr << "Failed to create Vulkan debug messenger: " << result << endl;
        return VK_NULL_HANDLE;
    }

    return debugMessenger;
}

void destroyVulkanDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger)
{
    if (debugMessenger != VK_NULL_HANDLE) {
        auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)getVKFunction(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (vkDestroyDebugUtilsMessengerEXT) {
            vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }
    }
}

