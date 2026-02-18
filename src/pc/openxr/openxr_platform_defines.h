#ifndef OPENXR_PLATFORM_DEFINES_H
#define OPENXR_PLATFORM_DEFINES_H

#ifdef __ANDROID__
#include <jni.h>
#include <EGL/egl.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#elif defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#else
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#endif

// Include OpenXR headers after defining platform macros
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#endif // OPENXR_PLATFORM_DEFINES_H
