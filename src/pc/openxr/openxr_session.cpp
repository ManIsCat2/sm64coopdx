#include "openxr_session.h"
#include "openxr_instance.h"
#include <iostream>
#include <cmath>
#include <EGL/egl.h>

// Define platform for EGL binding on non-Android platforms
#ifndef __ANDROID__
#define XR_USE_PLATFORM_EGL
#endif

XrSession createXRSession(
    XrInstance instance,
    XrSystemId systemID,
    EGLDisplay display,
    EGLContext context
)
{
    XrSession session;

    // IMPORTANT: Query graphics requirements before creating session
    // This is mandatory per OpenXR spec
#ifdef __ANDROID__
    PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetOpenGLESGraphicsRequirementsKHR = nullptr;
    XrResult result = xrGetInstanceProcAddr(
        instance,
        "xrGetOpenGLESGraphicsRequirementsKHR",
        (PFN_xrVoidFunction*)&xrGetOpenGLESGraphicsRequirementsKHR
    );
    
    if (result != XR_SUCCESS || !xrGetOpenGLESGraphicsRequirementsKHR) {
        std::cerr << "Failed to get xrGetOpenGLESGraphicsRequirementsKHR function: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{};
    graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
    
    result = xrGetOpenGLESGraphicsRequirementsKHR(instance, systemID, &graphicsRequirements);
    if (result != XR_SUCCESS) {
        std::cerr << "Failed to get OpenGL ES graphics requirements: " << result << std::endl;
        return XR_NULL_HANDLE;
    }
#else
    // For Linux desktop builds using EGL extension
    PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetOpenGLESGraphicsRequirementsKHR = nullptr;
    XrResult result = xrGetInstanceProcAddr(
        instance,
        "xrGetOpenGLESGraphicsRequirementsKHR",
        (PFN_xrVoidFunction*)&xrGetOpenGLESGraphicsRequirementsKHR
    );
    
    if (result != XR_SUCCESS || !xrGetOpenGLESGraphicsRequirementsKHR) {
        std::cerr << "Failed to get xrGetOpenGLESGraphicsRequirementsKHR function: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{};
    graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
    
    result = xrGetOpenGLESGraphicsRequirementsKHR(instance, systemID, &graphicsRequirements);
    if (result != XR_SUCCESS) {
        std::cerr << "Failed to get OpenGL ES graphics requirements: " << result << std::endl;
        return XR_NULL_HANDLE;
    }
#endif

    std::cout << "OpenGL ES graphics requirements:";
    std::cout << "  Min API version: " << XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported) << "." << XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported);
    std::cout << "  Max API version: " << XR_VERSION_MAJOR(graphicsRequirements.maxApiVersionSupported) << "." << XR_VERSION_MINOR(graphicsRequirements.maxApiVersionSupported);
    std::cout << std::endl;
    
#ifdef __ANDROID__
    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{};
    graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
    graphicsBinding.display = display;
    graphicsBinding.config = (EGLConfig)0;  // Not required for OpenXR
    graphicsBinding.context = context;
#else
    // For Linux desktop builds
    XrGraphicsBindingEGLMNDX graphicsBinding{};
    graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_EGL_MNDX;
    graphicsBinding.getProcAddress = eglGetProcAddress;
    graphicsBinding.display = display;
    graphicsBinding.config = (EGLConfig)0;
    graphicsBinding.context = context;
#endif

    XrSessionCreateInfo sessionCreateInfo{};
    sessionCreateInfo.type = XR_TYPE_SESSION_CREATE_INFO;
    sessionCreateInfo.next = &graphicsBinding;
    sessionCreateInfo.createFlags = 0;
    sessionCreateInfo.systemId = systemID;

    result = xrCreateSession(instance, &sessionCreateInfo, &session);

    if (result != XR_SUCCESS)
    {
        std::cerr << "Failed to create OpenXR session: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    std::cout << "OpenXR session created successfully with OpenGL ES binding" << std::endl;

    return session;
}

void destroyXRSession(XrSession session)
{
    if (session != XR_NULL_HANDLE) {
        xrDestroySession(session);
    }
}

XrSpace createXRSpace(XrSession session)
{
    XrSpace space;

    XrReferenceSpaceCreateInfo spaceCreateInfo{};
    spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;


    // Rotate 90 degrees to the left (around +Y)
    XrQuaternionf rotation;
    rotation.x = 0.0f;
    rotation.y = -sinf(M_PI / 4.0f);  // +90°/2
    rotation.z = 0.0f;
    rotation.w = cosf(M_PI / 4.0f);

    spaceCreateInfo.poseInReferenceSpace = { { rotation.x, rotation.y, rotation.z, rotation.w }, { 5.0f, -3.0f, 5.0f } };

    XrResult result = xrCreateReferenceSpace(session, &spaceCreateInfo, &space);

    if (result != XR_SUCCESS)
    {
        std::cerr << "Failed to create OpenXR reference space: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    std::cout << "OpenXR reference space created successfully" << std::endl;

    return space;
}

void destroyXRSpace(XrSpace space)
{
    if (space != XR_NULL_HANDLE) {
        xrDestroySpace(space);
    }
}

