#ifdef OPENXR_ENABLED

#include "controller_openxr.h"
#include "pc/openxr/openxr_manager.h"

#include <ultra64.h>
#include <PR/os_cont.h>

#include <iostream>
#include <vector>
#include <cmath>

#define OPENXR_CONTROLLER_DEBUG 0

// OpenXR headers are already included via openxr_manager.h -> openxr_instance.h usually, 
// but we might need to include them directly if not.
// Assuming openxr_manager.h provides access or we need to include them.
#include <openxr/openxr.h>

// Helper macros for OpenXR error checking
#define XR_CHECK(x) { \
    XrResult result = (x); \
    if (XR_FAILED(result)) { \
        std::cerr << "OpenXR Error: " << #x << " failed with " << result << std::endl; \
    } \
}

static bool s_initialized = false;
static XrActionSet s_actionSet = XR_NULL_HANDLE;

// Actions
static XrAction s_actionJump = XR_NULL_HANDLE;
static XrAction s_actionAttack = XR_NULL_HANDLE;
static XrAction s_actionCrouch = XR_NULL_HANDLE; // Z-trigger
static XrAction s_actionCamera = XR_NULL_HANDLE; // Right stick / C-buttons
static XrAction s_actionMovement = XR_NULL_HANDLE; // Left stick
static XrAction s_actionStart = XR_NULL_HANDLE;
static XrAction s_actionL = XR_NULL_HANDLE;
static XrAction s_actionR = XR_NULL_HANDLE;

// Paths
static XrPath s_pathHandLeft = XR_NULL_PATH;
static XrPath s_pathHandRight = XR_NULL_PATH;

static void controller_openxr_init(void) {
    if (s_initialized) return;
    
    if (!openxr_is_initialized()) {
        std::cerr << "OpenXR not initialized, skipping controller init" << std::endl;
        return;
    }

    XrInstance instance = openxr_get_instance();
    XrSession session = openxr_get_session();
    
    if (instance == XR_NULL_HANDLE || session == XR_NULL_HANDLE) {
        std::cerr << "OpenXR instance or session not ready" << std::endl;
        return;
    }

    std::cout << "Initializing OpenXR Controller Backend..." << std::endl;

    // Create Action Set
    XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(actionSetInfo.actionSetName, "gameplay");
    strcpy(actionSetInfo.localizedActionSetName, "Gameplay");
    actionSetInfo.priority = 0;
    XR_CHECK(xrCreateActionSet(instance, &actionSetInfo, &s_actionSet));

    // Create Actions
    // Jump (A)
    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
    actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionInfo.actionName, "jump");
    strcpy(actionInfo.localizedActionName, "Jump");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionJump));

    // Attack (B)
    strcpy(actionInfo.actionName, "attack");
    strcpy(actionInfo.localizedActionName, "Attack");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionAttack));

    // Crouch (Z)
    strcpy(actionInfo.actionName, "crouch");
    strcpy(actionInfo.localizedActionName, "Crouch");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionCrouch));

    // Start
    strcpy(actionInfo.actionName, "start");
    strcpy(actionInfo.localizedActionName, "Start");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionStart));

    // L Button
    strcpy(actionInfo.actionName, "l_button");
    strcpy(actionInfo.localizedActionName, "L Button");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionL));

    // R Button
    strcpy(actionInfo.actionName, "r_button");
    strcpy(actionInfo.localizedActionName, "R Button");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionR));

    // Movement (Stick)
    actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    strcpy(actionInfo.actionName, "movement");
    strcpy(actionInfo.localizedActionName, "Movement");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionMovement));

    // Camera (Stick/C-buttons)
    strcpy(actionInfo.actionName, "camera");
    strcpy(actionInfo.localizedActionName, "Camera");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionCamera));

    // Suggest Bindings
    XrPath pathInteractionProfile = XR_NULL_PATH;
    
    // Oculus Touch
    xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller", &pathInteractionProfile);
    
    std::vector<XrActionSuggestedBinding> bindings;
    auto addBinding = [&](XrAction action, const char* pathStr) {
        XrPath path;
        xrStringToPath(instance, pathStr, &path);
        bindings.push_back({action, path});
    };

    // Oculus Bindings
    addBinding(s_actionJump, "/user/hand/right/input/a/click");
    addBinding(s_actionAttack, "/user/hand/right/input/b/click"); // Or X/Y on left?
    addBinding(s_actionCrouch, "/user/hand/left/input/trigger/value"); // Left trigger for Z
    addBinding(s_actionCrouch, "/user/hand/right/input/trigger/value"); // Right trigger for Z (alternative)
    addBinding(s_actionStart, "/user/hand/left/input/menu/click"); // Menu button
    addBinding(s_actionMovement, "/user/hand/left/input/thumbstick");
    addBinding(s_actionMovement, "/user/hand/left/input/thumbstick");
    addBinding(s_actionCamera, "/user/hand/right/input/thumbstick");
    addBinding(s_actionL, "/user/hand/left/input/squeeze/value"); // Grab/Grip
    addBinding(s_actionR, "/user/hand/right/input/squeeze/value"); // Grab/Grip

    XrInteractionProfileSuggestedBinding suggestedBindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggestedBindings.interactionProfile = pathInteractionProfile;
    suggestedBindings.suggestedBindings = bindings.data();
    suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
    XR_CHECK(xrSuggestInteractionProfileBindings(instance, &suggestedBindings));

    // Attach Action Set to Session
    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &s_actionSet;
    XR_CHECK(xrAttachSessionActionSets(session, &attachInfo));

    // Get subaction paths
    xrStringToPath(instance, "/user/hand/left", &s_pathHandLeft);
    xrStringToPath(instance, "/user/hand/right", &s_pathHandRight);

    s_initialized = true;
}

static void controller_openxr_read(OSContPad *pad) {
    if (!s_initialized) return;

    XrSession session = openxr_get_session();
    if (session == XR_NULL_HANDLE) return;

    // Sync Actions
    XrActiveActionSet activeActionSet{s_actionSet, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    
    if (XR_FAILED(xrSyncActions(session, &syncInfo))) {
        return;
    }

    // Helper to get boolean state
    auto getBool = [&](XrAction action) -> bool {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &getInfo, &state))) {
            return state.currentState && state.isActive;
        }
        return false;
    };

    // Helper to get vector2 state
    auto getVec2 = [&](XrAction action) -> XrVector2f {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_SUCCEEDED(xrGetActionStateVector2f(session, &getInfo, &state))) {
            if (state.isActive) return state.currentState;
        }
        return {0.0f, 0.0f};
    };

    // Map Inputs
    if (getBool(s_actionJump)) pad->button |= A_BUTTON;
    if (getBool(s_actionAttack)) pad->button |= B_BUTTON;
    if (getBool(s_actionCrouch)) pad->button |= Z_TRIG;
    if (getBool(s_actionStart)) pad->button |= START_BUTTON;
    if (getBool(s_actionL)) pad->button |= L_TRIG;
    if (getBool(s_actionR)) pad->button |= R_TRIG;

    // Movement Stick
    XrVector2f movement = getVec2(s_actionMovement);
    // Map -1.0..1.0 to -80..80 (approx)
    pad->stick_x = (s8)(movement.x * 80.0f);
    pad->stick_y = (s8)(movement.y * 80.0f);

    // Camera Stick -> C-Buttons
    XrVector2f camera = getVec2(s_actionCamera);
    float threshold = 0.5f;
    if (camera.x > threshold) pad->button |= R_CBUTTONS;
    if (camera.x < -threshold) pad->button |= L_CBUTTONS;
    if (camera.y > threshold) pad->button |= U_CBUTTONS;
    if (camera.y < -threshold) pad->button |= D_CBUTTONS;
}

static u32 controller_openxr_rawkey(void) {
    return VK_INVALID;
}

static void controller_openxr_rumble_play(float str, float time) {
    // TODO: Implement haptics
}

static void controller_openxr_rumble_stop(void) {
    // TODO: Implement haptics
}

static void controller_openxr_bind(void) {
    // No-op for now
}

static void controller_openxr_shutdown(void) {
    if (!s_initialized) return;
    
    // OpenXR resources are generally cleaned up by destroying the instance/session
    // But we could destroy actions here if we wanted to be pedantic.
    s_initialized = false;
}

struct ControllerAPI controller_openxr = {
    0, // vkbase (not used for this backend really)
    controller_openxr_init,
    controller_openxr_read,
    controller_openxr_rawkey,
    controller_openxr_rumble_play,
    controller_openxr_rumble_stop,
    controller_openxr_bind,
    controller_openxr_shutdown
};

#endif // OPENXR_ENABLED
