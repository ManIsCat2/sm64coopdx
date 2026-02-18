#ifndef OPENXR_SESSION_H
#define OPENXR_SESSION_H

#include "openxr_platform_defines.h"

#ifdef __ANDROID__
#include <EGL/egl.h>
typedef EGLDisplay XRNativeDisplayType;
typedef EGLContext XRNativeContextType;
#elif defined(_WIN32)
typedef HDC XRNativeDisplayType;
typedef HGLRC XRNativeContextType;
#else
// Linux/PC
typedef Display* XRNativeDisplayType;
typedef GLXContext XRNativeContextType;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Session management
XrSession createXRSession(
    XrInstance instance,
    XrSystemId systemID,
    XRNativeDisplayType display,
    XRNativeContextType context
);
void destroyXRSession(XrSession session);

// Space management
XrSpace createXRSpace(XrSession session);
XrSpace createXRSpaceWithRotation(XrSession session, XrQuaternionf rotation, XrVector3f position);
void destroyXRSpace(XrSpace space);

#ifdef __cplusplus
}
#endif

#endif // OPENXR_SESSION_H

