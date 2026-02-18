#include "openxr_platform_defines.h"
#include "openxr_instance.h"
#include <iostream>
#include <cstring>
#include <vector>

#ifdef __ANDROID__
#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#endif

// Application information
static const char* const applicationName = "sm64coopdx";
static const unsigned int majorVersion = 1;
static const unsigned int minorVersion = 0;
static const unsigned int patchVersion = 0;

#ifdef __ANDROID__
// Store Android activity global reference for cleanup
static jobject gAndroidActivityGlobalRef = nullptr;
#endif

// Required extensions
static const std::vector<const char*> requiredExtensions = {
#ifdef __ANDROID__
    "XR_KHR_opengl_es_enable",
#else
    "XR_KHR_opengl_enable",
#endif
    "XR_EXT_debug_utils"
};

// Optional extensions - will be enabled if available
static const std::vector<const char*> optionalExtensions = {
    "XR_META_virtual_keyboard",
    "XR_FB_render_model",
    "XR_EXT_hand_tracking",
    "XR_FB_hand_tracking_aim"
};

PFN_xrVoidFunction getXRFunction(XrInstance instance, const char* name)
{
    PFN_xrVoidFunction func;

    XrResult result = xrGetInstanceProcAddr(instance, name, &func);

    if (result != XR_SUCCESS)
    {
        std::cerr << "Failed to load OpenXR extension function '" << name << "': " << result << std::endl;
        return nullptr;
    }

    return func;
}

XrBool32 handleXRError(
    XrDebugUtilsMessageSeverityFlagsEXT severity,
    XrDebugUtilsMessageTypeFlagsEXT type,
    const XrDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData
)
{
    std::cerr << "OpenXR ";

    switch (type)
    {
    case XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT :
        std::cerr << "general ";
        break;
    case XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT :
        std::cerr << "validation ";
        break;
    case XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT :
        std::cerr << "performance ";
        break;
    case XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT :
        std::cerr << "conformance ";
        break;
    }

    switch (severity)
    {
    case XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT :
        std::cerr << "(verbose): ";
        break;
    case XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT :
        std::cerr << "(info): ";
        break;
    case XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT :
        std::cerr << "(warning): ";
        break;
    case XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT :
        std::cerr << "(error): ";
        break;
    }

    std::cerr << callbackData->message << std::endl;

    return XR_FALSE;
}

XrInstance createXRInstance()
{
#ifdef __ANDROID__
    // STEP 1: Initialize the OpenXR loader for Android
    PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
    if (xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                               (PFN_xrVoidFunction*)&initializeLoader) == XR_SUCCESS &&
        initializeLoader != nullptr)
    {
        // Get JNIEnv from SDL
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        if (!env) {
            std::cerr << "Failed to get JNIEnv from SDL" << std::endl;
            return XR_NULL_HANDLE;
        }

        // Get JavaVM from JNIEnv
        JavaVM* vm = nullptr;
        if (env->GetJavaVM(&vm) != JNI_OK || !vm) {
            std::cerr << "Failed to get JavaVM from JNIEnv" << std::endl;
            return XR_NULL_HANDLE;
        }

        // Get Android activity context from SDL
        jobject activity = (jobject)SDL_AndroidGetActivity();
        if (!activity) {
            std::cerr << "Failed to get Android activity from SDL" << std::endl;
            return XR_NULL_HANDLE;
        }

        // Create a global reference to the activity context so it persists
        jobject activityGlobalRef = env->NewGlobalRef(activity);
        
        // Clean up the local reference returned by SDL
        env->DeleteLocalRef(activity);

        if (!activityGlobalRef) {
            std::cerr << "Failed to create global reference to activity" << std::endl;
            return XR_NULL_HANDLE;
        }

        XrLoaderInitInfoAndroidKHR loaderInitInfo{};
        loaderInitInfo.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
        loaderInitInfo.next = nullptr;
        loaderInitInfo.applicationVM = vm;
        loaderInitInfo.applicationContext = activityGlobalRef;

        XrResult initResult = initializeLoader((const XrLoaderInitInfoBaseHeaderKHR*)&loaderInitInfo);
        if (initResult != XR_SUCCESS)
        {
            std::cerr << "xrInitializeLoaderKHR failed: " << initResult << std::endl;
            env->DeleteGlobalRef(activityGlobalRef);
            return XR_NULL_HANDLE;
        }
        
        // Store global reference for cleanup later
        gAndroidActivityGlobalRef = activityGlobalRef;
        
        std::cout << "OpenXR loader initialized successfully with Android VM and context" << std::endl;
    }
    else
    {
        std::cerr << "Failed to load xrInitializeLoaderKHR function pointer." << std::endl;
        return XR_NULL_HANDLE;
    }
#endif

    // Enumerate supported extensions
    uint32_t extensionCount = 0;
    if (xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr) != XR_SUCCESS) {
        std::cerr << "Failed to enumerate OpenXR extensions count" << std::endl;
        return XR_NULL_HANDLE;
    }

    std::vector<XrExtensionProperties> supportedExtensions(extensionCount);
    for (auto& ext : supportedExtensions) {
        ext.type = XR_TYPE_EXTENSION_PROPERTIES;
        ext.next = nullptr;
    }

    if (xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, supportedExtensions.data()) != XR_SUCCESS) {
        std::cerr << "Failed to enumerate OpenXR extensions" << std::endl;
        return XR_NULL_HANDLE;
    }

    std::cout << "Supported OpenXR extensions:" << std::endl;
    for (const auto& ext : supportedExtensions) {
        std::cout << "  " << ext.extensionName << " (v" << ext.extensionVersion << ")" << std::endl;
    }

    // Build list of enabled extensions
    std::vector<const char*> enabledExtensions;
    
    // Check required extensions
    for (const auto& required : requiredExtensions) {
        bool found = false;
        for (const auto& supported : supportedExtensions) {
            if (strcmp(required, supported.extensionName) == 0) {
                enabledExtensions.push_back(required);
                found = true;
                break;
            }
        }
        
        if (!found) {
            std::cerr << "Error: Required OpenXR extension not supported: " << required << std::endl;
            return XR_NULL_HANDLE;
        }
    }

    // Check optional extensions
    for (const auto& optional : optionalExtensions) {
        for (const auto& supported : supportedExtensions) {
            if (strcmp(optional, supported.extensionName) == 0) {
                enabledExtensions.push_back(optional);
                std::cout << "Enabling optional extension: " << optional << std::endl;
                break;
            }
        }
    }

    std::cout << "Enabled OpenXR extensions:" << std::endl;
    for (const auto& ext : enabledExtensions) {
        std::cout << "  " << ext << std::endl;
    }

    XrInstance instance;

    XrInstanceCreateInfo instanceCreateInfo{};
    instanceCreateInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.createFlags = 0;
    strncpy(instanceCreateInfo.applicationInfo.applicationName, applicationName, XR_MAX_APPLICATION_NAME_SIZE);
    // XR_MAKE_VERSION returns 64-bit, but applicationVersion is 32-bit. 
    // We just use 1 for now or cast to uint32_t if we want to pack it, but OpenXR 1.0 headers usually expect 32-bit here.
    instanceCreateInfo.applicationInfo.applicationVersion = 1; 
    strncpy(instanceCreateInfo.applicationInfo.engineName, applicationName, XR_MAX_ENGINE_NAME_SIZE);
    instanceCreateInfo.applicationInfo.engineVersion = 1;
    instanceCreateInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
    instanceCreateInfo.enabledApiLayerCount = 0;
    instanceCreateInfo.enabledApiLayerNames = nullptr;
    instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    instanceCreateInfo.enabledExtensionNames = enabledExtensions.data();

    XrResult result = xrCreateInstance(&instanceCreateInfo, &instance);

    if (result != XR_SUCCESS)
    {
        std::cerr << "Failed to create OpenXR instance: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    std::cout << "OpenXR instance created successfully" << std::endl;

    return instance;
}

void destroyXRInstance(XrInstance instance)
{
    if (instance != XR_NULL_HANDLE) {
        xrDestroyInstance(instance);
    }
    
#ifdef __ANDROID__
    // Clean up Android activity global reference
    if (gAndroidActivityGlobalRef) {
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        if (env) {
            env->DeleteGlobalRef(gAndroidActivityGlobalRef);
            gAndroidActivityGlobalRef = nullptr;
        }
    }
#endif
}

XrDebugUtilsMessengerEXT createXRDebugMessenger(XrInstance instance)
{
    XrDebugUtilsMessengerEXT debugMessenger;

    XrDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo{};
    debugMessengerCreateInfo.type = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugMessengerCreateInfo.messageSeverities = XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugMessengerCreateInfo.messageTypes = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
    debugMessengerCreateInfo.userCallback = handleXRError;
    debugMessengerCreateInfo.userData = nullptr;

    auto xrCreateDebugUtilsMessengerEXT = (PFN_xrCreateDebugUtilsMessengerEXT)getXRFunction(instance, "xrCreateDebugUtilsMessengerEXT");

    if (!xrCreateDebugUtilsMessengerEXT) {
        std::cerr << "Failed to get xrCreateDebugUtilsMessengerEXT function" << std::endl;
        return XR_NULL_HANDLE;
    }

    XrResult result = xrCreateDebugUtilsMessengerEXT(instance, &debugMessengerCreateInfo, &debugMessenger);

    if (result != XR_SUCCESS)
    {
        std::cerr << "Failed to create OpenXR debug messenger: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    return debugMessenger;
}

void destroyXRDebugMessenger(XrInstance instance, XrDebugUtilsMessengerEXT debugMessenger)
{
    if (debugMessenger != XR_NULL_HANDLE) {
        auto xrDestroyDebugUtilsMessengerEXT = (PFN_xrDestroyDebugUtilsMessengerEXT)getXRFunction(instance, "xrDestroyDebugUtilsMessengerEXT");
        if (xrDestroyDebugUtilsMessengerEXT) {
            xrDestroyDebugUtilsMessengerEXT(debugMessenger);
        }
    }
}

XrSystemId getXRSystem(XrInstance instance)
{
    XrSystemId systemID;

    XrSystemGetInfo systemGetInfo{};
    systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult result = xrGetSystem(instance, &systemGetInfo, &systemID);

    if (result != XR_SUCCESS)
    {
        std::cerr << "Failed to get OpenXR system: " << result << std::endl;
        return XR_NULL_SYSTEM_ID;
    }

    std::cout << "OpenXR system acquired successfully" << std::endl;

    return systemID;
}

