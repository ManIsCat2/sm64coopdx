#include "openxr_manager.h"
#include "openxr_instance.h"
#include "openxr_session.h"
#include "vr_renderer.h"
#include "openxr_keyboard.h"
#include "pc/controller/controller_openxr.h"

#include <iostream>
#include <set>
#include <string>
#include <cmath>

#include <SDL2/SDL.h>
#include <EGL/egl.h>


// Forward declaration from vr_renderer.cpp
extern "C" void vr_renderer_set_frame_state(XrFrameState frameState);

// Global OpenXR/EGL state
static struct {
    bool initialized;
    bool sessionRunning;
    XrInstance xrInstance;
    XrDebugUtilsMessengerEXT xrDebugMessenger;
    XrSystemId xrSystemId;
    EGLDisplay eglDisplay;
    EGLContext eglContext;
    XrSession xrSession;
    XrSpace xrSpace;
    XrSessionState sessionState;
    XrFrameState frameState;
    
    // Cached pose data
    XrPosef headPose;
    bool poseValid;
} g_openxr_state = {
    false,
    false,
    XR_NULL_HANDLE,
    XR_NULL_HANDLE,
    XR_NULL_SYSTEM_ID,
    EGL_NO_DISPLAY,
    EGL_NO_CONTEXT,
    XR_NULL_HANDLE,
    XR_NULL_HANDLE,
    XR_SESSION_STATE_UNKNOWN,
    {},
    {},
    false
};

int openxr_init(void)
{
    if (g_openxr_state.initialized) {
        std::cout << "OpenXR already initialized" << std::endl;
        return 1;
    }

    std::cout << "Initializing OpenXR context..." << std::endl;

    // Create OpenXR instance
    g_openxr_state.xrInstance = createXRInstance();
    if (g_openxr_state.xrInstance == XR_NULL_HANDLE) {
        std::cerr << "Failed to create OpenXR instance. VR will not be available." << std::endl;
        return 0;
    }

    // Create debug messenger
    g_openxr_state.xrDebugMessenger = createXRDebugMessenger(g_openxr_state.xrInstance);

    // Get system
    g_openxr_state.xrSystemId = getXRSystem(g_openxr_state.xrInstance);
    if (g_openxr_state.xrSystemId == XR_NULL_SYSTEM_ID) {
        std::cerr << "Failed to get OpenXR system. VR will not be available." << std::endl;
        openxr_shutdown();
        return 0;
    }

    // Get current EGL display and context from SDL
    g_openxr_state.eglDisplay = eglGetCurrentDisplay();
    if (g_openxr_state.eglDisplay == EGL_NO_DISPLAY) {
        std::cerr << "Failed to get current EGL display. VR will not be available.";
        openxr_shutdown();
        return 0;
    }

    g_openxr_state.eglContext = eglGetCurrentContext();
    if (g_openxr_state.eglContext == EGL_NO_CONTEXT) {
        std::cerr << "Failed to get current EGL context. VR will not be available.";
        openxr_shutdown();
        return 0;
    }

    std::cout << "Got EGL display and context";

    // Create OpenXR session with OpenGL ES binding
    g_openxr_state.xrSession = createXRSession(
        g_openxr_state.xrInstance,
        g_openxr_state.xrSystemId,
        g_openxr_state.eglDisplay,
        g_openxr_state.eglContext
    );

    if (g_openxr_state.xrSession == XR_NULL_HANDLE) {
        std::cerr << "Failed to create OpenXR session. VR will not be available.";
        openxr_shutdown();
        return 0;
    }

    // Create reference space
    g_openxr_state.xrSpace = createXRSpace(g_openxr_state.xrSession);
    if (g_openxr_state.xrSpace == XR_NULL_HANDLE) {
        std::cerr << "Failed to create OpenXR reference space. VR will not be available.";
        openxr_shutdown();
        return 0;
    }

    g_openxr_state.initialized = true;
    std::cout << "OpenXR context initialized successfully with OpenGL ES!";
    
    // Initialize VR renderer
    if (!vr_renderer_init()) {
        std::cerr << "Warning: Failed to initialize VR renderer. VR rendering will not be available.";
        // Don't fail the whole init, just continue without VR rendering
    }

    // Initialize Virtual Keyboard
    if (!OpenXRKeyboard::GetInstance().Init(g_openxr_state.xrInstance, g_openxr_state.xrSession)) {
        std::cerr << "Warning: Failed to initialize Virtual Keyboard.";
    }

    return 1;
}


void openxr_shutdown(void)
{
    if (!g_openxr_state.initialized && 
        g_openxr_state.xrInstance == XR_NULL_HANDLE) {
        return;
    }

    std::cout << "Shutting down OpenXR context..." << std::endl;
    
    // Shutdown VR renderer first
    OpenXRKeyboard::GetInstance().Shutdown();
    vr_renderer_shutdown();

    // Destroy in reverse order of creation
    destroyXRSpace(g_openxr_state.xrSpace);
    destroyXRSession(g_openxr_state.xrSession);
    destroyXRDebugMessenger(g_openxr_state.xrInstance, g_openxr_state.xrDebugMessenger);
    destroyXRInstance(g_openxr_state.xrInstance);

    // Reset state
    g_openxr_state.initialized = false;
    g_openxr_state.xrInstance = XR_NULL_HANDLE;
    g_openxr_state.xrDebugMessenger = XR_NULL_HANDLE;
    g_openxr_state.xrSystemId = XR_NULL_SYSTEM_ID;
    g_openxr_state.eglDisplay = EGL_NO_DISPLAY;
    g_openxr_state.eglContext = EGL_NO_CONTEXT;
    g_openxr_state.xrSession = XR_NULL_HANDLE;
    g_openxr_state.xrSpace = XR_NULL_HANDLE;

    std::cout << "OpenXR context shutdown complete" << std::endl;
}

int openxr_is_initialized(void)
{
    return g_openxr_state.initialized ? 1 : 0;
}

// Helper function to convert quaternion to Euler angles (in degrees)
static void quaternion_to_euler(const XrQuaternionf& q, float* yaw, float* pitch, float* roll)
{
    // Roll (x-axis rotation)
    float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
    float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    *roll = std::atan2(sinr_cosp, cosr_cosp) * (180.0f / M_PI);

    // Pitch (y-axis rotation)
    float sinp = 2.0f * (q.w * q.y - q.z * q.x);
    if (std::abs(sinp) >= 1.0f)
        *pitch = std::copysign(M_PI / 2.0f, sinp) * (180.0f / M_PI); // use 90 degrees if out of range
    else
        *pitch = std::asin(sinp) * (180.0f / M_PI);

    // Yaw (z-axis rotation)
    float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
    float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    *yaw = std::atan2(siny_cosp, cosy_cosp) * (180.0f / M_PI);
}

int openxr_update(void)
{
    if (!g_openxr_state.initialized || g_openxr_state.xrSession == XR_NULL_HANDLE) {
        return 0;
    }

    // Poll OpenXR events
    XrEventDataBuffer eventData{};
    eventData.type = XR_TYPE_EVENT_DATA_BUFFER;

    while (xrPollEvent(g_openxr_state.xrInstance, &eventData) == XR_SUCCESS) {
        switch (eventData.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                XrEventDataSessionStateChanged* stateChangeEvent = 
                    reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
                g_openxr_state.sessionState = stateChangeEvent->state;
                
                std::cout << "OpenXR session state changed to: " << stateChangeEvent->state << std::endl;

                switch (stateChangeEvent->state) {
                    case XR_SESSION_STATE_READY: {
                        // Begin session
                        XrSessionBeginInfo beginInfo{};
                        beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
                        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        
                        XrResult result = xrBeginSession(g_openxr_state.xrSession, &beginInfo);
                        if (result == XR_SUCCESS) {
                            g_openxr_state.sessionRunning = true;
                            std::cout << "OpenXR session started" << std::endl;
                        } else {
                            std::cerr << "Failed to begin OpenXR session: " << result << std::endl;
                        }
                        break;
                    }
                    case XR_SESSION_STATE_STOPPING: {
                        // End session
                        xrEndSession(g_openxr_state.xrSession);
                        g_openxr_state.sessionRunning = false;
                        std::cout << "OpenXR session stopped" << std::endl;
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                std::cout << "OpenXR instance loss pending" << std::endl;
                return 0;
            case XR_TYPE_EVENT_DATA_VIRTUAL_KEYBOARD_COMMIT_TEXT_META: {
                XrEventDataVirtualKeyboardCommitTextMETA* commitEvent = 
                    reinterpret_cast<XrEventDataVirtualKeyboardCommitTextMETA*>(&eventData);
                std::cout << "Virtual keyboard commit text: " << commitEvent->text << std::endl;
                
                SDL_Event event;
                event.type = SDL_TEXTINPUT;
                event.text.timestamp = SDL_GetTicks();
                event.text.windowID = 0;
                strncpy(event.text.text, commitEvent->text, SDL_TEXTINPUTEVENT_TEXT_SIZE);
                event.text.text[SDL_TEXTINPUTEVENT_TEXT_SIZE - 1] = '\0';
                SDL_PushEvent(&event);
                break;
            }
            case XR_TYPE_EVENT_DATA_VIRTUAL_KEYBOARD_BACKSPACE_META: {
                XrEventDataVirtualKeyboardBackspaceMETA* backspaceEvent = 
                    reinterpret_cast<XrEventDataVirtualKeyboardBackspaceMETA*>(&eventData);
                std::cout << "Virtual keyboard backspace" << std::endl;
                
                SDL_Event event;

                event.type = SDL_KEYDOWN;
                event.key.timestamp = SDL_GetTicks();
                event.key.windowID = 0;
                event.key.state = SDL_PRESSED;
                event.key.repeat = 0;
                event.key.keysym.scancode = SDL_SCANCODE_BACKSPACE;
                event.key.keysym.sym = SDLK_BACKSPACE;
                event.key.keysym.mod = KMOD_NONE;
                SDL_PushEvent(&event);
                
                event.type = SDL_KEYUP;
                event.key.timestamp = SDL_GetTicks();
                event.key.windowID = 0;
                event.key.state = SDL_RELEASED;
                event.key.repeat = 0;
                event.key.keysym.scancode = SDL_SCANCODE_BACKSPACE;
                event.key.keysym.sym = SDLK_BACKSPACE;
                event.key.keysym.mod = KMOD_NONE;
                SDL_PushEvent(&event);
                break;
            }
            case XR_TYPE_EVENT_DATA_VIRTUAL_KEYBOARD_ENTER_META: {
                SDL_Event event;
                
                event.type = SDL_KEYDOWN;
                event.key.timestamp = SDL_GetTicks();
                event.key.windowID = 0;
                event.key.state = SDL_PRESSED;
                event.key.repeat = 0;
                event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                event.key.keysym.sym = SDLK_RETURN;
                event.key.keysym.mod = KMOD_NONE;
                SDL_PushEvent(&event);
                
                event.type = SDL_KEYUP;
                event.key.timestamp = SDL_GetTicks();
                event.key.windowID = 0;
                event.key.state = SDL_RELEASED;
                event.key.repeat = 0;
                event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                event.key.keysym.sym = SDLK_RETURN;
                event.key.keysym.mod = KMOD_NONE;
                SDL_PushEvent(&event);
                break;
            }
            default:
                break;
        }
        
        eventData.type = XR_TYPE_EVENT_DATA_BUFFER;
    }

    if (!g_openxr_state.sessionRunning) {
        return 0;
    }

    // Wait for next frame
    XrFrameWaitInfo frameWaitInfo{};
    frameWaitInfo.type = XR_TYPE_FRAME_WAIT_INFO;
    
    XrFrameState frameState{};
    frameState.type = XR_TYPE_FRAME_STATE;
    
    XrResult result = xrWaitFrame(g_openxr_state.xrSession, &frameWaitInfo, &frameState);
    if (result != XR_SUCCESS) {
        return 0;
    }

    g_openxr_state.frameState = frameState;
    
    // Update VR renderer with frame state
    vr_renderer_set_frame_state(frameState);

    // Update Virtual Keyboard with keyboard reference space (not game's rotated space)
    XrSpace keyboardSpace = controller_openxr_get_keyboard_space();
    XrSpace targetSpace = (keyboardSpace != XR_NULL_HANDLE) ? keyboardSpace : g_openxr_state.xrSpace;
    
    XrPosef headPoseInTargetSpace = { {0,0,0,1}, {0,0,0} };
    
    // Get head pose in target space
    if (frameState.shouldRender) {
        XrViewLocateInfo viewLocateInfo{};
        viewLocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
        viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        viewLocateInfo.displayTime = frameState.predictedDisplayTime;
        viewLocateInfo.space = targetSpace;
        
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCount = 0;
        XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        
        if (xrLocateViews(g_openxr_state.xrSession, &viewLocateInfo, &viewState, 2, &viewCount, views) == XR_SUCCESS) {
            // Average the pose of both eyes to get head pose
            headPoseInTargetSpace.orientation.x = (views[0].pose.orientation.x + views[1].pose.orientation.x) / 2.0f;
            headPoseInTargetSpace.orientation.y = (views[0].pose.orientation.y + views[1].pose.orientation.y) / 2.0f;
            headPoseInTargetSpace.orientation.z = (views[0].pose.orientation.z + views[1].pose.orientation.z) / 2.0f;
            headPoseInTargetSpace.orientation.w = (views[0].pose.orientation.w + views[1].pose.orientation.w) / 2.0f;
            
            headPoseInTargetSpace.position.x = (views[0].pose.position.x + views[1].pose.position.x) / 2.0f;
            headPoseInTargetSpace.position.y = (views[0].pose.position.y + views[1].pose.position.y) / 2.0f;
            headPoseInTargetSpace.position.z = (views[0].pose.position.z + views[1].pose.position.z) / 2.0f;
            
            // Normalize quaternion
            float length = std::sqrt(
                headPoseInTargetSpace.orientation.x * headPoseInTargetSpace.orientation.x +
                headPoseInTargetSpace.orientation.y * headPoseInTargetSpace.orientation.y +
                headPoseInTargetSpace.orientation.z * headPoseInTargetSpace.orientation.z +
                headPoseInTargetSpace.orientation.w * headPoseInTargetSpace.orientation.w
            );
            
            if (length > 0.0f) {
                headPoseInTargetSpace.orientation.x /= length;
                headPoseInTargetSpace.orientation.y /= length;
                headPoseInTargetSpace.orientation.z /= length;
                headPoseInTargetSpace.orientation.w /= length;
            }
        }
    }
    
    OpenXRKeyboard::GetInstance().Update(targetSpace, frameState.predictedDisplayTime, headPoseInTargetSpace);

    // Begin frame
    XrFrameBeginInfo frameBeginInfo{};
    frameBeginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
    
    result = xrBeginFrame(g_openxr_state.xrSession, &frameBeginInfo);
    if (result != XR_SUCCESS) {
        std::cerr << "xrBeginFrame failed: " << result;
        return 0;
    }
    
    static int first_begin = 1;
    if (first_begin) {
        std::cout << "xrBeginFrame succeeded, shouldRender = " << frameState.shouldRender << std::endl;
        first_begin = 0;
    }

    // Locate views (get head pose)
    if (frameState.shouldRender) {
        XrViewState viewState{};
        viewState.type = XR_TYPE_VIEW_STATE;
        
        uint32_t viewCount = 2;
        XrView views[2] = {
            {XR_TYPE_VIEW},
            {XR_TYPE_VIEW}
        };
        
        XrViewLocateInfo viewLocateInfo{};
        viewLocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
        viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        viewLocateInfo.displayTime = frameState.predictedDisplayTime;
        viewLocateInfo.space = g_openxr_state.xrSpace;
        
        result = xrLocateViews(g_openxr_state.xrSession, &viewLocateInfo, &viewState, viewCount, &viewCount, views);
        
        if (result == XR_SUCCESS && viewCount > 0) {
            // Average the pose of both eyes to get head pose
            g_openxr_state.headPose.orientation.x = (views[0].pose.orientation.x + views[1].pose.orientation.x) / 2.0f;
            g_openxr_state.headPose.orientation.y = (views[0].pose.orientation.y + views[1].pose.orientation.y) / 2.0f;
            g_openxr_state.headPose.orientation.z = (views[0].pose.orientation.z + views[1].pose.orientation.z) / 2.0f;
            g_openxr_state.headPose.orientation.w = (views[0].pose.orientation.w + views[1].pose.orientation.w) / 2.0f;
            
            g_openxr_state.headPose.position.x = (views[0].pose.position.x + views[1].pose.position.x) / 2.0f;
            g_openxr_state.headPose.position.y = (views[0].pose.position.y + views[1].pose.position.y) / 2.0f;
            g_openxr_state.headPose.position.z = (views[0].pose.position.z + views[1].pose.position.z) / 2.0f;
            
            // Normalize quaternion
            float length = std::sqrt(
                g_openxr_state.headPose.orientation.x * g_openxr_state.headPose.orientation.x +
                g_openxr_state.headPose.orientation.y * g_openxr_state.headPose.orientation.y +
                g_openxr_state.headPose.orientation.z * g_openxr_state.headPose.orientation.z +
                g_openxr_state.headPose.orientation.w * g_openxr_state.headPose.orientation.w
            );
            
            if (length > 0.0f) {
                g_openxr_state.headPose.orientation.x /= length;
                g_openxr_state.headPose.orientation.y /= length;
                g_openxr_state.headPose.orientation.z /= length;
                g_openxr_state.headPose.orientation.w /= length;
            }
            
            g_openxr_state.poseValid = true;
        } else {
            g_openxr_state.poseValid = false;
        }
    }

    // Note: xrEndFrame() will be called by vr_renderer_end_frame() when VR rendering is active.
    // OpenXR requires that every xrBeginFrame() has a matching xrEndFrame().
    // The VR renderer is responsible for calling xrEndFrame() with the rendered layers.
    // If VR rendering fails or isn't active, it should still call xrEndFrame() with 0 layers.
    
    return 1;
}

int openxr_get_head_rotation(float* yaw, float* pitch, float* roll)
{
    if (!g_openxr_state.initialized || !g_openxr_state.poseValid) {
        return 0;
    }

    quaternion_to_euler(g_openxr_state.headPose.orientation, yaw, pitch, roll);
    return 1;
}

int openxr_get_head_position(float* x, float* y, float* z)
{
    if (!g_openxr_state.initialized || !g_openxr_state.poseValid) {
        return 0;
    }

    *x = g_openxr_state.headPose.position.x;
    *y = g_openxr_state.headPose.position.y;
    *z = g_openxr_state.headPose.position.z;
    
    return 1;
}

int64_t openxr_get_predicted_display_time(void)
{
    return (int64_t)g_openxr_state.frameState.predictedDisplayTime;
}

// Expose OpenXR handles for VR renderer
XrInstance openxr_get_instance(void)
{
    return g_openxr_state.xrInstance;
}

XrSession openxr_get_session(void)
{
    return g_openxr_state.xrSession;
}

XrSpace openxr_get_space(void)
{
    return g_openxr_state.xrSpace;
}

XrSystemId openxr_get_system_id(void)
{
    return g_openxr_state.xrSystemId;
}

EGLDisplay openxr_get_egl_display(void)
{
    return g_openxr_state.eglDisplay;
}

EGLContext openxr_get_egl_context(void)
{
    return g_openxr_state.eglContext;
}

int openxr_end_frame_empty(void)
{
    if (!g_openxr_state.initialized || !g_openxr_state.sessionRunning) {
        return 0;
    }

    // Submit frame with no layers (required to match xrBeginFrame)
    XrFrameEndInfo frameEndInfo{};
    frameEndInfo.type = XR_TYPE_FRAME_END_INFO;
    frameEndInfo.displayTime = g_openxr_state.frameState.predictedDisplayTime;
    frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    frameEndInfo.layerCount = 0;  // No layers
    frameEndInfo.layers = nullptr;
    
    XrResult result = xrEndFrame(g_openxr_state.xrSession, &frameEndInfo);
    
    if (result != XR_SUCCESS) {
        std::cerr << "Failed to end OpenXR frame (empty): " << result;
        return 0;
    }
    
    return 1;
}
