#include "openxr_session.h"
#include "openxr_instance.h"
#include <iostream>
#include <cmath>
// #include <EGL/egl.h> // Handled by openxr_platform_defines.h

XrSession createXRSession(
    XrInstance instance,
    XrSystemId systemID,
    XRNativeDisplayType display,
    XRNativeContextType context
)
{
    XrSession session;
    XrResult result;

    // IMPORTANT: Query graphics requirements before creating session
    // This is mandatory per OpenXR spec
#ifdef __ANDROID__
    PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetOpenGLESGraphicsRequirementsKHR = nullptr;
    result = xrGetInstanceProcAddr(
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

    std::cout << "OpenGL ES graphics requirements:";
    std::cout << "  Min API version: " << XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported) << "." << XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported);
    std::cout << "  Max API version: " << XR_VERSION_MAJOR(graphicsRequirements.maxApiVersionSupported) << "." << XR_VERSION_MINOR(graphicsRequirements.maxApiVersionSupported);
    std::cout << std::endl;

    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{};
    graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
    graphicsBinding.display = display;
    graphicsBinding.config = (EGLConfig)0;  // Not required for OpenXR
    graphicsBinding.context = context;

#elif defined(_WIN32)
    // For Windows builds using OpenGL
    PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR = nullptr;
    result = xrGetInstanceProcAddr(
        instance,
        "xrGetOpenGLGraphicsRequirementsKHR",
        (PFN_xrVoidFunction*)&xrGetOpenGLGraphicsRequirementsKHR
    );
    
    if (result != XR_SUCCESS || !xrGetOpenGLGraphicsRequirementsKHR) {
        std::cerr << "Failed to get xrGetOpenGLGraphicsRequirementsKHR function: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    XrGraphicsRequirementsOpenGLKHR graphicsRequirements{};
    graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
    
    result = xrGetOpenGLGraphicsRequirementsKHR(instance, systemID, &graphicsRequirements);
    if (result != XR_SUCCESS) {
        std::cerr << "Failed to get OpenGL graphics requirements: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    std::cout << "OpenGL graphics requirements:";
    std::cout << "  Min API version: " << XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported) << "." << XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported);
    std::cout << "  Max API version: " << XR_VERSION_MAJOR(graphicsRequirements.maxApiVersionSupported) << "." << XR_VERSION_MINOR(graphicsRequirements.maxApiVersionSupported);
    std::cout << std::endl;

    XrGraphicsBindingOpenGLWin32KHR graphicsBinding{};
    graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
    graphicsBinding.hDC = display;
    graphicsBinding.hGLRC = context;
#else
    // For Linux desktop builds using OpenGL extension
    PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR = nullptr;
    result = xrGetInstanceProcAddr(
        instance,
        "xrGetOpenGLGraphicsRequirementsKHR",
        (PFN_xrVoidFunction*)&xrGetOpenGLGraphicsRequirementsKHR
    );
    
    if (result != XR_SUCCESS || !xrGetOpenGLGraphicsRequirementsKHR) {
        std::cerr << "Failed to get xrGetOpenGLGraphicsRequirementsKHR function: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    XrGraphicsRequirementsOpenGLKHR graphicsRequirements{};
    graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
    
    result = xrGetOpenGLGraphicsRequirementsKHR(instance, systemID, &graphicsRequirements);
    if (result != XR_SUCCESS) {
        std::cerr << "Failed to get OpenGL graphics requirements: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    std::cout << "OpenGL graphics requirements:";
    std::cout << "  Min API version: " << XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported) << "." << XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported);
    std::cout << "  Max API version: " << XR_VERSION_MAJOR(graphicsRequirements.maxApiVersionSupported) << "." << XR_VERSION_MINOR(graphicsRequirements.maxApiVersionSupported);
    std::cout << std::endl;

    XrGraphicsBindingOpenGLXlibKHR graphicsBinding{};
    graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR;
    graphicsBinding.xDisplay = display;
    graphicsBinding.glxDrawable = 0; // glXGetCurrentDrawable(); // Optional/Not strictly needed if context is enough? Wait, OpenXR spec says: "glxDrawable is a GLXDrawable. The GLXDrawable MUST be valid and MUST be compatible with the glxContext." 
    // Actually, usually we pass the window or a drawable. Let's see if we can get it or if 0 works (some runtimes accept 0 if they create their own surface). 
    // However, looking at examples, it's usually required. 
    // I can get the drawable from glXGetCurrentDrawable() if I include GL/glx.h.
    // openxr_platform_defines.h includes GL/glx.h. 
    graphicsBinding.glxDrawable = glXGetCurrentDrawable();
    graphicsBinding.glxContext = context;
    
    std::cout << "GLX Drawable: " << (void*)graphicsBinding.glxDrawable << std::endl;
    std::cout << "GLX Context: " << (void*)graphicsBinding.glxContext << std::endl;
    graphicsBinding.visualid = 0;
    
    // Get VisualID from the drawable
    XWindowAttributes xwa;
    Status s = XGetWindowAttributes((Display*)display, graphicsBinding.glxDrawable, &xwa);
    if (s) {
        graphicsBinding.visualid = XVisualIDFromVisual(xwa.visual);
        std::cout << "Visual ID: " << graphicsBinding.visualid << std::endl;
    } else {
        std::cerr << "Error: Failed to get window attributes for drawable " << std::hex << graphicsBinding.glxDrawable << std::dec << std::endl;
    }
    
    // NOTE: glXGetCurrentDrawable requires a current context. Since we are initializing in a thread where context is current (implied by passing context), this should work.
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

    std::cout << "OpenXR session created successfully" << std::endl;

    return session;
}

void destroyXRSession(XrSession session)
{
    if (session != XR_NULL_HANDLE) {
        xrDestroySession(session);
    }
}

XrSpace createXRSpaceWithRotation(XrSession session, XrQuaternionf rotation, XrVector3f position)
{
    XrSpace space;

    XrReferenceSpaceCreateInfo spaceCreateInfo{};
    spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;

    spaceCreateInfo.poseInReferenceSpace = { { rotation.x, rotation.y, rotation.z, rotation.w }, { position.x, position.y, position.z } };

    XrResult result = xrCreateReferenceSpace(session, &spaceCreateInfo, &space);

    if (result != XR_SUCCESS)
    {
        std::cerr << "Failed to create OpenXR reference space: " << result << std::endl;
        return XR_NULL_HANDLE;  
    }

    std::cout << "OpenXR reference space created successfully" << std::endl;

    return space;
}

XrSpace createXRSpace(XrSession session)
{
    XrQuaternionf rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    XrVector3f position = {0.0f, 0.0f, 0.0f};
    return createXRSpaceWithRotation(session, rotation, position);
}

void destroyXRSpace(XrSpace space)
{
    if (space != XR_NULL_HANDLE) {
        xrDestroySpace(space);
    }
}

