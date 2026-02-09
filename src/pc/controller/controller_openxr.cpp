#ifdef OPENXR_ENABLED

#include "controller_openxr.h"

#include <ultra64.h>
#include <PR/os_cont.h>
#include "pc/openxr/openxr_manager.h"
#include "pc/openxr/openxr_keyboard.h"

extern "C" {

#include "pc/pc_main.h"
#include "pc/djui/djui.h"
#include "pc/configfile.h"
}

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

#ifndef XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_LEFT_META
#define XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_LEFT_META (XrVirtualKeyboardInputSourceMETA)1
#endif
#ifndef XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_RIGHT_META
#define XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_RIGHT_META (XrVirtualKeyboardInputSourceMETA)2
#endif

static bool s_initialized = false;
static XrActionSet s_actionSet = XR_NULL_HANDLE;

// Actions
static XrAction s_actionJump = XR_NULL_HANDLE;
static XrAction s_actionAttack = XR_NULL_HANDLE;
static XrAction s_actionX = XR_NULL_HANDLE;
static XrAction s_actionY = XR_NULL_HANDLE;
static XrAction s_actionLeftTrigger = XR_NULL_HANDLE;
static XrAction s_actionRightTrigger = XR_NULL_HANDLE;
static XrAction s_actionCamera = XR_NULL_HANDLE;
static XrAction s_actionMovement = XR_NULL_HANDLE;
static XrAction s_actionStart = XR_NULL_HANDLE;
static XrAction s_actionL = XR_NULL_HANDLE;
static XrAction s_actionR = XR_NULL_HANDLE;
static XrAction s_actionStickClickLeft = XR_NULL_HANDLE;
static XrAction s_actionStickClickRight = XR_NULL_HANDLE;

// Paths
static XrPath s_pathHandLeft = XR_NULL_PATH;
static XrPath s_pathHandRight = XR_NULL_PATH;

// Keyboard interaction (controller-based)
static XrAction s_actionPoseLeft = XR_NULL_PATH;
static XrAction s_actionPoseRight = XR_NULL_PATH;
static XrAction s_actionSelectLeft = XR_NULL_PATH;
static XrAction s_actionSelectRight = XR_NULL_PATH;
static XrSpace s_spacePoseLeft = XR_NULL_HANDLE;
static XrSpace s_spacePoseRight = XR_NULL_HANDLE;

// Separate reference space for keyboard (STAGE without rotation/offset)
static XrSpace s_keyboardReferenceSpace = XR_NULL_HANDLE;

// Hand tracking extension functions
static PFN_xrCreateHandTrackerEXT xrCreateHandTrackerEXT = nullptr;
static PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT = nullptr;
static PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT = nullptr;

// Hand tracking handles and state
static XrHandTrackerEXT s_handTrackerLeft = XR_NULL_HANDLE;
static XrHandTrackerEXT s_handTrackerRight = XR_NULL_HANDLE;
static XrHandJointLocationEXT s_jointLocationsLeft[XR_HAND_JOINT_COUNT_EXT];
static XrHandJointLocationEXT s_jointLocationsRight[XR_HAND_JOINT_COUNT_EXT];
static XrHandTrackingAimStateFB s_aimStateLeft{XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
static XrHandTrackingAimStateFB s_aimStateRight{XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
static XrHandJointLocationsEXT s_locationsLeft{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
static XrHandJointLocationsEXT s_locationsRight{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};

// Binding system (similar to SDL controller)
#define MAX_OPENXR_BINDS 32
#define MAX_OPENXR_BUTTONS 19  // Number of virtual buttons we support

static u32 num_openxr_binds = 0;
static u32 openxr_binds[MAX_OPENXR_BINDS][2] = { 0 };
static bool openxr_buttons[MAX_OPENXR_BUTTONS] = { false };
static u32 last_openxr_button = VK_INVALID;

// Helper function to update hand tracking and get aim state
static bool update_hand_tracking(XrHandTrackerEXT tracker, XrSpace baseSpace, XrTime time,
                                  XrHandJointLocationsEXT* locations,
                                  XrHandTrackingAimStateFB* aimState) {
    if (!xrLocateHandJointsEXT || tracker == XR_NULL_HANDLE) {
        return false;
    }
    
    // Reset aim state
    aimState->next = nullptr;
    
    // Chain aim state to locations
    locations->next = aimState;
    
    // Locate hand joints
    XrHandJointsLocateInfoEXT locateInfo{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
    locateInfo.baseSpace = baseSpace;
    locateInfo.time = time;
    
    if (XR_FAILED(xrLocateHandJointsEXT(tracker, &locateInfo, locations))) {
        return false;
    }
    
    // Check if tracking is active and position is valid
    if (!locations->isActive) {
        return false;
    }
    
    // Check if palm joint has valid position
    if (!(locations->jointLocations[XR_HAND_JOINT_PALM_EXT].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
        return false;
    }
    
    return true;
}

// Helper function to update button state and track presses for rawkey
static inline void update_openxr_button(const int i, const bool new_state) {
    const bool pressed = !openxr_buttons[i] && new_state;
    const bool unpressed = openxr_buttons[i] && !new_state;
    openxr_buttons[i] = new_state;
    if (pressed) {
        last_openxr_button = i;
        djui_interactable_on_key_down(VK_BASE_OPENXR + i);
    }
    if (unpressed) {
        djui_interactable_on_key_up(VK_BASE_OPENXR + i);
    }
}

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

    // X Button
    strcpy(actionInfo.actionName, "x_button");
    strcpy(actionInfo.localizedActionName, "X Button");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionX));

    // Y button
    actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionInfo.actionName, "y_button");
    strcpy(actionInfo.localizedActionName, "Y Button");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionY));

    // Left Trigger
    strcpy(actionInfo.actionName, "left_trigger");
    strcpy(actionInfo.localizedActionName, "Left Trigger");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionLeftTrigger));

    // Right Trigger
    strcpy(actionInfo.actionName, "right_trigger");
    strcpy(actionInfo.localizedActionName, "Right Trigger");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionRightTrigger));

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

    // Stick Click Left
    actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionInfo.actionName, "stick_click_left");
    strcpy(actionInfo.localizedActionName, "Stick Click Left");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionStickClickLeft));

    // Stick Click Right
    actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strcpy(actionInfo.actionName, "stick_click_right");
    strcpy(actionInfo.localizedActionName, "Stick Click Right");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionStickClickRight));

    // Aim Poses
    actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
    strcpy(actionInfo.actionName, "aim_left");
    strcpy(actionInfo.localizedActionName, "Aim Left");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionPoseLeft));

    strcpy(actionInfo.actionName, "aim_right");
    strcpy(actionInfo.localizedActionName, "Aim Right");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionPoseRight));

    // Select (Trigger) for controller keyboard input
    actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
    strcpy(actionInfo.actionName, "select_left");
    strcpy(actionInfo.localizedActionName, "Select Left");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionSelectLeft));

    strcpy(actionInfo.actionName, "select_right");
    strcpy(actionInfo.localizedActionName, "Select Right");
    XR_CHECK(xrCreateAction(s_actionSet, &actionInfo, &s_actionSelectRight));

    // Suggest Bindings
    XrPath pathInteractionProfile = XR_NULL_PATH;
    
    std::vector<XrActionSuggestedBinding> bindings;
    auto addBinding = [&](XrAction action, const char* pathStr) {
        XrPath path;
        xrStringToPath(instance, pathStr, &path);
        bindings.push_back({action, path});
    };

    // ===== Oculus Touch Controller Profile =====
    xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller", &pathInteractionProfile);
    
    // Controller Bindings
    addBinding(s_actionJump, "/user/hand/right/input/a/click");
    addBinding(s_actionAttack, "/user/hand/right/input/b/click");
    addBinding(s_actionX, "/user/hand/left/input/x/click");
    addBinding(s_actionLeftTrigger, "/user/hand/left/input/trigger/value");
    addBinding(s_actionRightTrigger, "/user/hand/right/input/trigger/value");
    addBinding(s_actionStart, "/user/hand/left/input/menu/click");
    addBinding(s_actionMovement, "/user/hand/left/input/thumbstick");
    addBinding(s_actionCamera, "/user/hand/right/input/thumbstick");
    addBinding(s_actionL, "/user/hand/left/input/squeeze/value");
    addBinding(s_actionR, "/user/hand/right/input/squeeze/value");
    addBinding(s_actionY, "/user/hand/left/input/y/click");
    addBinding(s_actionStickClickLeft, "/user/hand/left/input/thumbstick/click");
    addBinding(s_actionStickClickRight, "/user/hand/right/input/thumbstick/click");
    
    // Controller keyboard bindings (aim pose and trigger for selection)
    addBinding(s_actionPoseLeft, "/user/hand/left/input/aim/pose");
    addBinding(s_actionPoseRight, "/user/hand/right/input/aim/pose");
    addBinding(s_actionSelectLeft, "/user/hand/left/input/trigger/value");
    addBinding(s_actionSelectRight, "/user/hand/right/input/trigger/value");

    XrInteractionProfileSuggestedBinding suggestedBindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggestedBindings.interactionProfile = pathInteractionProfile;
    suggestedBindings.suggestedBindings = bindings.data();
    suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
    XR_CHECK(xrSuggestInteractionProfileBindings(instance, &suggestedBindings));

    // Note: XR_EXT_hand_interaction extension is not fully supported on this Quest runtime
    // Hand tracking for keyboard input will need to be implemented using the direct
    // hand tracking API (XR_EXT_hand_tracking + XR_FB_hand_tracking_aim) instead

    // Attach Action Set to Session
    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &s_actionSet;
    XR_CHECK(xrAttachSessionActionSets(session, &attachInfo));

    // Initialize hand tracking
    std::cout << "Initializing Hand Tracking..." << std::endl;
    
    // Get extension function pointers
    XR_CHECK(xrGetInstanceProcAddr(instance, "xrCreateHandTrackerEXT", (PFN_xrVoidFunction*)(&xrCreateHandTrackerEXT)));
    XR_CHECK(xrGetInstanceProcAddr(instance, "xrDestroyHandTrackerEXT", (PFN_xrVoidFunction*)(&xrDestroyHandTrackerEXT)));
    XR_CHECK(xrGetInstanceProcAddr(instance, "xrLocateHandJointsEXT", (PFN_xrVoidFunction*)(&xrLocateHandJointsEXT)));
    
    if (xrCreateHandTrackerEXT && xrLocateHandJointsEXT) {
        // Create left hand tracker
        XrHandTrackerCreateInfoEXT createInfo{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        createInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        createInfo.hand = XR_HAND_LEFT_EXT;
        if (XR_SUCCEEDED(xrCreateHandTrackerEXT(session, &createInfo, &s_handTrackerLeft))) {
            std::cout << "Left hand tracker created successfully" << std::endl;
        } else {
            std::cerr << "Failed to create left hand tracker" << std::endl;
        }
        
        // Create right hand tracker
        createInfo.hand = XR_HAND_RIGHT_EXT;
        if (XR_SUCCEEDED(xrCreateHandTrackerEXT(session, &createInfo, &s_handTrackerRight))) {
            std::cout << "Right hand tracker created successfully" << std::endl;
        } else {
            std::cerr << "Failed to create right hand tracker" << std::endl;
        }
        
        // Initialize joint locations structures
        s_locationsLeft.jointCount = XR_HAND_JOINT_COUNT_EXT;
        s_locationsLeft.jointLocations = s_jointLocationsLeft;
        s_locationsLeft.next = &s_aimStateLeft;
        
        s_locationsRight.jointCount = XR_HAND_JOINT_COUNT_EXT;
        s_locationsRight.jointLocations = s_jointLocationsRight;
        s_locationsRight.next = &s_aimStateRight;
    } else {
        std::cerr << "Hand tracking extensions not available" << std::endl;
    }

    // Create Action Spaces
    XrActionSpaceCreateInfo actionSpaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    actionSpaceInfo.action = s_actionPoseLeft;
    actionSpaceInfo.poseInActionSpace = {{0,0,0,1}, {0,0,0}};
    XR_CHECK(xrCreateActionSpace(session, &actionSpaceInfo, &s_spacePoseLeft));

    actionSpaceInfo.action = s_actionPoseRight;
    XR_CHECK(xrCreateActionSpace(session, &actionSpaceInfo, &s_spacePoseRight));



    // Create keyboard reference space (STAGE without rotation/offset)
    XrReferenceSpaceCreateInfo keyboardSpaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    keyboardSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    keyboardSpaceInfo.poseInReferenceSpace = {{0,0,0,1}, {0,0,0}};
    XR_CHECK(xrCreateReferenceSpace(session, &keyboardSpaceInfo, &s_keyboardReferenceSpace));

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

    // Helper to get vector2 state with active flag
    struct Vec2State {
        XrVector2f value;
        bool isActive;
    };
    auto getVec2 = [&](XrAction action) -> Vec2State {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_SUCCEEDED(xrGetActionStateVector2f(session, &getInfo, &state))) {
            return {state.currentState, static_cast<bool>(state.isActive)};
        }
        return {{0.0f, 0.0f}, false};
    };

    // Read raw button states and update button tracking
    bool btnA = getBool(s_actionJump);
    bool btnB = getBool(s_actionAttack);
    bool btnX = getBool(s_actionX);
    bool btnY = getBool(s_actionY);
    bool btnMenu = getBool(s_actionStart);
    bool btnLTrigger = getBool(s_actionLeftTrigger);
    bool btnRTrigger = getBool(s_actionRightTrigger);
    bool btnLSqueeze = getBool(s_actionL);
    bool btnRSqueeze = getBool(s_actionR);
    bool btnStickClickLeft = getBool(s_actionStickClickLeft);
    bool btnStickClickRight = getBool(s_actionStickClickRight);

    // Update button states for rawkey tracking
    update_openxr_button(0, btnA);          // VK_OPENXR_A
    update_openxr_button(1, btnB);          // VK_OPENXR_B
    update_openxr_button(2, btnX);          // VK_OPENXR_X
    update_openxr_button(3, btnY);          // VK_OPENXR_Y
    update_openxr_button(4, btnMenu);       // VK_OPENXR_MENU
    update_openxr_button(5, btnLTrigger);   // VK_OPENXR_L_TRIGGER
    update_openxr_button(6, btnRTrigger);   // VK_OPENXR_R_TRIGGER
    update_openxr_button(7, btnLSqueeze);   // VK_OPENXR_L_SQUEEZE
    update_openxr_button(8, btnRSqueeze);   // VK_OPENXR_R_SQUEEZE
    update_openxr_button(17, btnStickClickLeft); // VK_OPENXR_L_STICK_CLICK
    update_openxr_button(18, btnStickClickRight);// VK_OPENXR_R_STICK_CLICK

    // Read stick states
    Vec2State movement = getVec2(s_actionMovement);
    Vec2State camera = getVec2(s_actionCamera);
    float threshold = 0.5f;

    // Update stick direction button states
    update_openxr_button(9, movement.value.y > threshold);   // VK_OPENXR_L_STICK_UP
    update_openxr_button(10, movement.value.y < -threshold); // VK_OPENXR_L_STICK_DOWN
    update_openxr_button(11, movement.value.x < -threshold); // VK_OPENXR_L_STICK_LEFT
    update_openxr_button(12, movement.value.x > threshold);  // VK_OPENXR_L_STICK_RIGHT
    update_openxr_button(13, camera.value.y > threshold);    // VK_OPENXR_R_STICK_UP
    update_openxr_button(14, camera.value.y < -threshold);   // VK_OPENXR_R_STICK_DOWN
    update_openxr_button(15, camera.value.x < -threshold);   // VK_OPENXR_R_STICK_LEFT
    update_openxr_button(16, camera.value.x > threshold);    // VK_OPENXR_R_STICK_RIGHT

    // Apply bindings to pad
    u32 buttons_down = 0;
    for (u32 i = 0; i < num_openxr_binds; ++i) {
        if (openxr_buttons[openxr_binds[i][0]]) {
            buttons_down |= openxr_binds[i][1];
        }
    }
    
    // If no bindings are configured, use default mappings
    if (num_openxr_binds == 0) {
        // Default button mappings (same as original hardcoded behavior)
        if (btnA) buttons_down |= A_BUTTON;
        if (btnB) buttons_down |= B_BUTTON;
        if (btnLTrigger || btnRTrigger) buttons_down |= Z_TRIG;
        if (btnMenu) buttons_down |= START_BUTTON;
        if (btnLSqueeze) buttons_down |= L_TRIG;
        if (btnRSqueeze) buttons_down |= R_TRIG;
        
        // Default C-button mappings from right stick
        if (camera.value.x > threshold) buttons_down |= R_CBUTTONS;
        if (camera.value.x < -threshold) buttons_down |= L_CBUTTONS;
        if (camera.value.y > threshold) buttons_down |= U_CBUTTONS;
        if (camera.value.y < -threshold) buttons_down |= D_CBUTTONS;
    }
    
    pad->button |= buttons_down;

    // Handle stick movement from bindings
    const u32 xstick = buttons_down & STICK_XMASK;
    const u32 ystick = buttons_down & STICK_YMASK;
    if (xstick == STICK_LEFT)
        pad->stick_x = -128;
    else if (xstick == STICK_RIGHT)
        pad->stick_x = 127;
    if (ystick == STICK_DOWN)
        pad->stick_y = -128;
    else if (ystick == STICK_UP)
        pad->stick_y = 127;

    // Movement Stick - only update if OpenXR controller is active so vr controllers don't override bluetooth controller with 0
    if (movement.isActive) {
        pad->stick_x = (s8)(movement.value.x * 80.0f);
        pad->stick_y = (s8)(movement.value.y * 80.0f);
    }

    // Camera Stick -> C-Buttons (only apply if bindings are configured and didn't already handle C-buttons)
    if (num_openxr_binds > 0 && !(buttons_down & (U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS))) {
        if (camera.value.x > threshold) pad->button |= R_CBUTTONS;
        if (camera.value.x < -threshold) pad->button |= L_CBUTTONS;
        if (camera.value.y > threshold) pad->button |= U_CBUTTONS;
        if (camera.value.y < -threshold) pad->button |= D_CBUTTONS;
    }

    // Handle Y button for keyboard toggle
    static bool s_lastToggleState = false;
    // bool currentToggleState = getBool(s_actionY);
    bool leftHandMenuPressed = false;

    // Update hand tracking data every frame (needed for menu button detection)
    XrTime time = openxr_get_predicted_display_time();
    
    // Update hand tracking state
    if (xrLocateHandJointsEXT) {
        // Left hand
        if (update_hand_tracking(s_handTrackerLeft, s_keyboardReferenceSpace, time, &s_locationsLeft, &s_aimStateLeft)) {
            leftHandMenuPressed = (s_aimStateLeft.status & XR_HAND_TRACKING_AIM_MENU_PRESSED_BIT_FB) != 0;
        }
        
        // Right hand (update tracking data)
        update_hand_tracking(s_handTrackerRight, s_keyboardReferenceSpace, time, &s_locationsRight, &s_aimStateRight);
    }

    if (leftHandMenuPressed && !s_lastToggleState) {
        djui_chat_box_toggle();
        if (gDjuiChatBoxFocus) {
            openxr_show_keyboard();
        } else {
            openxr_hide_keyboard();
        }
    }
    s_lastToggleState = leftHandMenuPressed;

    // Send keyboard input only when keyboard is visible
    if (openxr_is_keyboard_visible()) {
        auto sendInput = [&](XrSpace space, XrAction selectAction, XrVirtualKeyboardInputSourceMETA source) {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            // Use keyboard reference space (STAGE without rotation/offset) instead of game's reference space
            if (XR_SUCCEEDED(xrLocateSpace(space, s_keyboardReferenceSpace, time, &location))) {
                if (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {                    
                    float selectValue = 0.0f;
                    XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
                    getInfo.action = selectAction;
                    XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};

                    if (XR_SUCCEEDED(xrGetActionStateFloat(session, &getInfo, &state))) {
                        if (state.isActive) selectValue = state.currentState;
                    }

                    bool pressed = selectValue > 0.5f;
                    XrPosef interactorRootPose = location.pose;

                    OpenXRKeyboard::GetInstance().SendInput(s_keyboardReferenceSpace, source, location.pose, pressed, &interactorRootPose);
                }
            }
        };

        // Send controller input (ray-based)
        sendInput(s_spacePoseLeft, s_actionSelectLeft, XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_LEFT_META);
        sendInput(s_spacePoseRight, s_actionSelectRight, XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_RIGHT_META);
        
        // Send hand tracking input for keyboard (using already-updated tracking data)
        if (xrLocateHandJointsEXT) {
            // Left hand
            if (s_locationsLeft.isActive) {
                bool pinching = (s_aimStateLeft.status & XR_HAND_TRACKING_AIM_INDEX_PINCHING_BIT_FB) != 0;
                XrPosef aimPose = s_aimStateLeft.aimPose;
                XrPosef palmPose = s_jointLocationsLeft[XR_HAND_JOINT_PALM_EXT].pose;
                
                OpenXRKeyboard::GetInstance().SendInput(
                    s_keyboardReferenceSpace, 
                    XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_LEFT_META, 
                    aimPose, 
                    pinching, 
                    &palmPose
                );
            }
            
            // Right hand
            if (s_locationsRight.isActive) {
                bool pinching = (s_aimStateRight.status & XR_HAND_TRACKING_AIM_INDEX_PINCHING_BIT_FB) != 0;
                XrPosef aimPose = s_aimStateRight.aimPose;
                XrPosef palmPose = s_jointLocationsRight[XR_HAND_JOINT_PALM_EXT].pose;
                
                OpenXRKeyboard::GetInstance().SendInput(
                    s_keyboardReferenceSpace, 
                    XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_RIGHT_META, 
                    aimPose, 
                    pinching, 
                    &palmPose
                );
            }
        }
    }
}

static u32 controller_openxr_rawkey(void) {
    if (last_openxr_button != VK_INVALID) {
        const u32 ret = last_openxr_button;
        last_openxr_button = VK_INVALID;
        return ret;
    }
    return VK_INVALID;
}

static void controller_openxr_rumble_play(float str, float time) {
    // TODO: Implement haptics
}

static void controller_openxr_rumble_stop(void) {
    // TODO: Implement haptics
}

static inline void controller_openxr_add_binds(const u32 mask, const u32 *btns) {
    for (u32 i = 0; i < MAX_BINDS; ++i) {
        if (btns[i] >= VK_BASE_OPENXR && btns[i] < VK_BASE_OPENXR + VK_SIZE && num_openxr_binds < MAX_OPENXR_BINDS) {
            openxr_binds[num_openxr_binds][0] = btns[i] - VK_BASE_OPENXR;
            openxr_binds[num_openxr_binds][1] = mask;
            ++num_openxr_binds;
        }
    }
}

static void controller_openxr_bind(void) {
    bzero(openxr_binds, sizeof(openxr_binds));
    num_openxr_binds = 0;

    controller_openxr_add_binds(A_BUTTON,     configKeyA);
    controller_openxr_add_binds(B_BUTTON,     configKeyB);
    controller_openxr_add_binds(X_BUTTON,     configKeyX);
    controller_openxr_add_binds(Y_BUTTON,     configKeyY);
    controller_openxr_add_binds(Z_TRIG,       configKeyZ);
    controller_openxr_add_binds(STICK_UP,     configKeyStickUp);
    controller_openxr_add_binds(STICK_LEFT,   configKeyStickLeft);
    controller_openxr_add_binds(STICK_DOWN,   configKeyStickDown);
    controller_openxr_add_binds(STICK_RIGHT,  configKeyStickRight);
    controller_openxr_add_binds(U_CBUTTONS,   configKeyCUp);
    controller_openxr_add_binds(L_CBUTTONS,   configKeyCLeft);
    controller_openxr_add_binds(D_CBUTTONS,   configKeyCDown);
    controller_openxr_add_binds(R_CBUTTONS,   configKeyCRight);
    controller_openxr_add_binds(L_TRIG,       configKeyL);
    controller_openxr_add_binds(R_TRIG,       configKeyR);
    controller_openxr_add_binds(START_BUTTON, configKeyStart);
    controller_openxr_add_binds(U_JPAD,       configKeyDUp);
    controller_openxr_add_binds(D_JPAD,       configKeyDDown);
    controller_openxr_add_binds(L_JPAD,       configKeyDLeft);
    controller_openxr_add_binds(R_JPAD,       configKeyDRight);
}

static void controller_openxr_shutdown(void) {
    if (!s_initialized) return;
    
    // Destroy hand trackers
    if (xrDestroyHandTrackerEXT) {
        if (s_handTrackerLeft != XR_NULL_HANDLE) {
            xrDestroyHandTrackerEXT(s_handTrackerLeft);
            s_handTrackerLeft = XR_NULL_HANDLE;
        }
        if (s_handTrackerRight != XR_NULL_HANDLE) {
            xrDestroyHandTrackerEXT(s_handTrackerRight);
            s_handTrackerRight = XR_NULL_HANDLE;
        }
    }
    
    // OpenXR resources are generally cleaned up by destroying the instance/session
    // But we could destroy actions here if we wanted to be pedantic.
    s_initialized = false;
}

struct ControllerAPI controller_openxr = {
    VK_BASE_OPENXR,
    controller_openxr_init,
    controller_openxr_read,
    controller_openxr_rawkey,
    controller_openxr_rumble_play,
    controller_openxr_rumble_stop,
    controller_openxr_bind,
    controller_openxr_shutdown
};

XrSpace controller_openxr_get_keyboard_space(void) {
    return s_keyboardReferenceSpace;
}

#endif // OPENXR_ENABLED
