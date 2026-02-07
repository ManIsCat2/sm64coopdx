#ifndef OPENXR_SESSION_H
#define OPENXR_SESSION_H

#include <EGL/egl.h>

#ifdef __ANDROID__
#include <jni.h>
#define XR_USE_PLATFORM_ANDROID
#else
#define XR_USE_PLATFORM_EGL
#endif
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

// Session management
XrSession createXRSession(
    XrInstance instance,
    XrSystemId systemID,
    EGLDisplay display,
    EGLContext context
);
void destroyXRSession(XrSession session);

// Space management
XrSpace createXRSpace(XrSession session);
void destroyXRSpace(XrSpace space);

#ifdef __cplusplus
}
#endif

#endif // OPENXR_SESSION_H

