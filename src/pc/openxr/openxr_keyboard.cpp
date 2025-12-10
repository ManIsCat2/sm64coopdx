#include "openxr_keyboard.h"
#include "openxr_keyboard_gltf.h"
#include "vr_renderer.h"

#include <iostream>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <map>

#ifdef USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/gl.h>
#endif

#define OXR(func) \
    { \
        XrResult result = (func); \
        if (result != XR_SUCCESS) { \
            std::cerr << "OpenXR error at " << __FILE__ << ":" << __LINE__ << " code: " << result << std::endl; \
        } \
    }

#define OXR_RET(func) \
    { \
        XrResult result = (func); \
        if (result != XR_SUCCESS) { \
            std::cerr << "OpenXR error at " << __FILE__ << ":" << __LINE__ << " code: " << result << std::endl; \
            return false; \
        } \
    }

OpenXRKeyboard& OpenXRKeyboard::GetInstance() {
    static OpenXRKeyboard instance;
    return instance;
}

bool OpenXRKeyboard::Init(XrInstance instance, XrSession session) {
    instance_ = instance;
    session_ = session;

    // Load function pointers
    OXR_RET(xrGetInstanceProcAddr(instance, "xrCreateVirtualKeyboardMETA", (PFN_xrVoidFunction*)&xrCreateVirtualKeyboardMETA_));
    OXR_RET(xrGetInstanceProcAddr(instance, "xrDestroyVirtualKeyboardMETA", (PFN_xrVoidFunction*)&xrDestroyVirtualKeyboardMETA_));
    OXR_RET(xrGetInstanceProcAddr(instance, "xrCreateVirtualKeyboardSpaceMETA", (PFN_xrVoidFunction*)&xrCreateVirtualKeyboardSpaceMETA_));
    OXR_RET(xrGetInstanceProcAddr(instance, "xrSuggestVirtualKeyboardLocationMETA", (PFN_xrVoidFunction*)&xrSuggestVirtualKeyboardLocationMETA_));
    OXR_RET(xrGetInstanceProcAddr(instance, "xrGetVirtualKeyboardScaleMETA", (PFN_xrVoidFunction*)&xrGetVirtualKeyboardScaleMETA_));
    OXR_RET(xrGetInstanceProcAddr(instance, "xrSetVirtualKeyboardModelVisibilityMETA", (PFN_xrVoidFunction*)&xrSetVirtualKeyboardModelVisibilityMETA_));
    OXR_RET(xrGetInstanceProcAddr(instance, "xrGetVirtualKeyboardModelAnimationStatesMETA", (PFN_xrVoidFunction*)&xrGetVirtualKeyboardModelAnimationStatesMETA_));
    OXR_RET(xrGetInstanceProcAddr(instance, "xrGetVirtualKeyboardDirtyTexturesMETA", (PFN_xrVoidFunction*)&xrGetVirtualKeyboardDirtyTexturesMETA_));
    OXR_RET(xrGetInstanceProcAddr(instance, "xrGetVirtualKeyboardTextureDataMETA", (PFN_xrVoidFunction*)&xrGetVirtualKeyboardTextureDataMETA_));
    OXR_RET(xrGetInstanceProcAddr(instance, "xrSendVirtualKeyboardInputMETA", (PFN_xrVoidFunction*)&xrSendVirtualKeyboardInputMETA_));
    OXR_RET(xrGetInstanceProcAddr(instance, "xrChangeVirtualKeyboardTextContextMETA", (PFN_xrVoidFunction*)&xrChangeVirtualKeyboardTextContextMETA_));

    // Load render model extension functions
    if (!InitRenderModelExtension()) {
        std::cerr << "Warning: Render model extension not available, keyboard model won't be rendered" << std::endl;
    }

    initialized_ = true;
    return CreateKeyboard();
}

void OpenXRKeyboard::Shutdown() {
    UnloadKeyboardModel();
    DestroyKeyboard();
    initialized_ = false;
    instance_ = XR_NULL_HANDLE;
    session_ = XR_NULL_HANDLE;
}

// Helper for matrix multiplication
static void MatrixMultiply(const float* a, const float* b, float* out) {
    float res[16];
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            res[col * 4 + row] = sum;
        }
    }
    memcpy(out, res, sizeof(float) * 16);
}

bool OpenXRKeyboard::CreateKeyboard() {
    if (!initialized_) {
        std::cerr << "Cannot create keyboard: not initialized" << std::endl;
        return false;
    }
    if (keyboardHandle_ != XR_NULL_HANDLE) return true;

    std::cout << "Creating virtual keyboard..." << std::endl;
    XrVirtualKeyboardCreateInfoMETA createInfo{XR_TYPE_VIRTUAL_KEYBOARD_CREATE_INFO_META};
    XrResult result = xrCreateVirtualKeyboardMETA_(session_, &createInfo, &keyboardHandle_);
    
    if (result != XR_SUCCESS) {
        std::cerr << "Failed to create virtual keyboard. Error code: " << result << std::endl;
        std::cerr << "This may indicate:" << std::endl;
        std::cerr << "  - XR_META_virtual_keyboard extension not supported by runtime" << std::endl;
        std::cerr << "  - Session not in correct state" << std::endl;
        std::cerr << "  - Runtime doesn't support virtual keyboard on this device" << std::endl;
        return false;
    }
    
    std::cout << "Virtual keyboard created successfully" << std::endl;

    XrVirtualKeyboardSpaceCreateInfoMETA spaceCreateInfo{XR_TYPE_VIRTUAL_KEYBOARD_SPACE_CREATE_INFO_META};
    spaceCreateInfo.space = XR_NULL_HANDLE; 
    
    return true;
}

bool OpenXRKeyboard::DestroyKeyboard() {
    if (keyboardHandle_ != XR_NULL_HANDLE) {
        if (xrDestroyVirtualKeyboardMETA_) {
            xrDestroyVirtualKeyboardMETA_(keyboardHandle_);
        }
        keyboardHandle_ = XR_NULL_HANDLE;
    }
    if (space_ != XR_NULL_HANDLE) {
        xrDestroySpace(space_);
        space_ = XR_NULL_HANDLE;
    }
    return true;
}

bool OpenXRKeyboard::ShowKeyboard() {
    if (!initialized_ || keyboardHandle_ == XR_NULL_HANDLE) return false;
    
    // Load the model on first show
    if (!modelLoaded_) {
        if (!LoadKeyboardModel()) {
            std::cerr << "Failed to load keyboard model, keyboard will show but won't render" << std::endl;
            // Continue anyway - the keyboard can still function for input even without rendering
        }
    }
    
    XrVirtualKeyboardModelVisibilitySetInfoMETA modelVisibility{XR_TYPE_VIRTUAL_KEYBOARD_MODEL_VISIBILITY_SET_INFO_META};
    modelVisibility.visible = XR_TRUE;
    OXR_RET(xrSetVirtualKeyboardModelVisibilityMETA_(keyboardHandle_, &modelVisibility));
    
    isVisible_ = true;
    recenterRequested_ = true;
    return true;
}

bool OpenXRKeyboard::HideKeyboard() {
    if (!initialized_ || keyboardHandle_ == XR_NULL_HANDLE) return false;

    XrVirtualKeyboardModelVisibilitySetInfoMETA modelVisibility{XR_TYPE_VIRTUAL_KEYBOARD_MODEL_VISIBILITY_SET_INFO_META};
    modelVisibility.visible = XR_FALSE;
    OXR_RET(xrSetVirtualKeyboardModelVisibilityMETA_(keyboardHandle_, &modelVisibility));
    
    isVisible_ = false;
    return true;
}

// Matrix math helpers
static void MultiplyMatrix(const float* a, const float* b, float* result) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            result[col * 4 + row] = sum;
        }
    }
}

static void ComposeModelMatrix(const XrPosef& pose, float scale, float* matrix) {
    const XrQuaternionf& q = pose.orientation;
    const XrVector3f& p = pose.position;

    // Quaternion to Rotation Matrix (Column Major)
    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    matrix[0] = (1.0f - 2.0f * (yy + zz)) * scale;
    matrix[1] = (2.0f * (xy + wz)) * scale;
    matrix[2] = (2.0f * (xz - wy)) * scale;
    matrix[3] = 0.0f;

    matrix[4] = (2.0f * (xy - wz)) * scale;
    matrix[5] = (1.0f - 2.0f * (xx + zz)) * scale;
    matrix[6] = (2.0f * (yz + wx)) * scale;
    matrix[7] = 0.0f;

    matrix[8] = (2.0f * (xz + wy)) * scale;
    matrix[9] = (2.0f * (yz - wx)) * scale;
    matrix[10] = (1.0f - 2.0f * (xx + yy)) * scale;
    matrix[11] = 0.0f;

    matrix[12] = p.x;
    matrix[13] = p.y;
    matrix[14] = p.z;
    matrix[15] = 1.0f;
}

static void PoseToViewMatrix(const XrPosef& pose, float* matrix) {
    // Convert quaternion to rotation matrix
    const XrQuaternionf& q = pose.orientation;
    const XrVector3f& p = pose.position;
    
    // Create rotation matrix from quaternion
    float rotMatrix[16];
    memset(rotMatrix, 0, 16 * sizeof(float));
    
    rotMatrix[0] = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    rotMatrix[1] = 2.0f * (q.x * q.y + q.w * q.z);
    rotMatrix[2] = 2.0f * (q.x * q.z - q.w * q.y);
    rotMatrix[3] = 0.0f;
    
    rotMatrix[4] = 2.0f * (q.x * q.y - q.w * q.z);
    rotMatrix[5] = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    rotMatrix[6] = 2.0f * (q.y * q.z + q.w * q.x);
    rotMatrix[7] = 0.0f;
    
    rotMatrix[8] = 2.0f * (q.x * q.z + q.w * q.y);
    rotMatrix[9] = 2.0f * (q.y * q.z - q.w * q.x);
    rotMatrix[10] = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    rotMatrix[11] = 0.0f;
    
    rotMatrix[12] = 0.0f;
    rotMatrix[13] = 0.0f;
    rotMatrix[14] = 0.0f;
    rotMatrix[15] = 1.0f;
    
    // Invert the view matrix (view = inverse of pose)
    // For a rigid body transform, inverse is transpose of rotation and negated position
    matrix[0] = rotMatrix[0];
    matrix[1] = rotMatrix[4];
    matrix[2] = rotMatrix[8];
    matrix[3] = 0.0f;
    
    matrix[4] = rotMatrix[1];
    matrix[5] = rotMatrix[5];
    matrix[6] = rotMatrix[9];
    matrix[7] = 0.0f;
    
    matrix[8] = rotMatrix[2];
    matrix[9] = rotMatrix[6];
    matrix[10] = rotMatrix[10];
    matrix[11] = 0.0f;
    
    matrix[12] = -(rotMatrix[0] * p.x + rotMatrix[1] * p.y + rotMatrix[2] * p.z);
    matrix[13] = -(rotMatrix[4] * p.x + rotMatrix[5] * p.y + rotMatrix[6] * p.z);
    matrix[14] = -(rotMatrix[8] * p.x + rotMatrix[9] * p.y + rotMatrix[10] * p.z);
    matrix[15] = 1.0f;
}

void OpenXRKeyboard::Render(int eye) {
    if (!isVisible_ || !initialized_ || !modelLoaded_) return;
    
    // Use our shader program
    if (shaderProgram_ == 0) return;
    
    // Save GL state - COMPREHENSIVE to prevent DJUI crash
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepthMask;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    GLint prevProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    
    // Save blend function
    GLint prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcAlpha, prevBlendDstAlpha;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);
    
    // Save texture state
    GLint prevActiveTexture;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    GLint prevTexture2D;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2D);
    
    // Save VAO and buffer bindings
    GLint prevVAO;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    GLint prevArrayBuffer;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    GLint prevElementArrayBuffer;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElementArrayBuffer);
    
    // Set state for keyboard
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE); // Ensure we can write to depth buffer (and clear it)
    glEnable(GL_BLEND);
    // Use premultiplied alpha blending for text glyphs
    // Meta's keyboard textures use premultiplied alpha
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE); // Disable culling to be safe
    glDisable(GL_SCISSOR_TEST); // Disable scissor to clear entire depth buffer
    
    // Clear depth buffer to render on top of everything
    glClear(GL_DEPTH_BUFFER_BIT);
    
    glUseProgram(shaderProgram_);
    
    float viewMatrix[16];
    float projMatrix[16];
    
    if (!vr_renderer_get_projection_matrix_ext(eye, 0.05f, 100.0f, projMatrix)) {
        goto restore_state;
    }

    // Calculate View Matrix relative to the keyboard's space
    // We need to locate the views in the same space as the keyboard (currentSpace_)
    if (currentSpace_ == XR_NULL_HANDLE) {
         // Fallback if no space (shouldn't happen if initialized)
         if (!vr_renderer_get_view_matrix(eye, viewMatrix)) goto restore_state;
    } else {
        XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        viewLocateInfo.displayTime = currentDisplayTime_; // Use the display time from Update()
        viewLocateInfo.space = currentSpace_;
        
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCount = 0;
        XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        
        if (xrLocateViews(session_, &viewLocateInfo, &viewState, 2, &viewCount, views) != XR_SUCCESS) {
             goto restore_state;
        }
        
        // Check if pose is valid
        if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0 ||
            (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0) {
             goto restore_state;
        }
        
        PoseToViewMatrix(views[eye].pose, viewMatrix);
    }

    // Get current keyboard location
    VirtualKeyboardLocation location;
    
    // Use stored frame state
    if (currentSpace_ != XR_NULL_HANDLE && currentDisplayTime_ != 0) {
        if (GetLocation(currentSpace_, currentDisplayTime_, &location)) {
            
            // Calculate Model Matrix
            float modelMatrix[16];
            ComposeModelMatrix(location.pose, location.scale, modelMatrix);
            
            // MVP is now calculated per-mesh
            GLint mvpLoc = glGetUniformLocation(shaderProgram_, "uModelViewProjection");
            
            // Iterate through all nodes in the model state (Meta's pattern)
            for (auto& nodeState : modelState_.nodeStates) {
                // Check if node has a model to render (Meta's pattern: Node->Model->Surfaces)
                if (nodeState.node == nullptr) {
                    continue; // No node data
                }
                
                const auto* node = nodeState.node;
                
                // Skip nodes without a model (e.g., transform-only nodes)
                if (node->model == nullptr || node->model->surfaces.empty()) {
                    continue;
                }

                // Get the animated node transform
                float* nodeGlobalTransform = nodeState.globalTransform;
                
                // Calculate per-node MVP part: P * V * M_keyboard_root * M_node_global
                float keyboardRoot_NodeGlobal_Matrix[16];
                MatrixMultiply(modelMatrix, nodeGlobalTransform, keyboardRoot_NodeGlobal_Matrix);
                
                float viewModelMatrix[16];
                MatrixMultiply(viewMatrix, keyboardRoot_NodeGlobal_Matrix, viewModelMatrix);
                
                float meshMVP[16];
                MatrixMultiply(projMatrix, viewModelMatrix, meshMVP);

                // Find the first mesh for this node from our map
                int meshIdx = -1;
                // Calculate node index from iterator position
                int nodeIdx = &nodeState - &modelState_.nodeStates[0];
                auto meshIt = modelState_.nodeToFirstMesh.find(nodeIdx);
                if (meshIt != modelState_.nodeToFirstMesh.end()) {
                    meshIdx = meshIt->second;
                } else {
                    // Skip nodes that don't have any meshes
                    continue;
                }
                
                if (meshIdx >= 0 && meshIdx < meshes_.size()) {
                    const auto& mesh = meshes_[meshIdx];
                    
                    if (mesh.vao != 0) {
                        // Bind texture
                        auto it = textures_.find(mesh.textureId);
                        if (it != textures_.end()) {
                            glActiveTexture(GL_TEXTURE0);
                            glBindTexture(GL_TEXTURE_2D, it->second.textureId);
                            glUniform1i(glGetUniformLocation(shaderProgram_, "Texture0"), 0);
                        } else {
                            // Use fallback texture (ID 0) if mesh texture not found
                            auto fallbackIt = textures_.find(0);
                            if (fallbackIt != textures_.end()) {
                                glActiveTexture(GL_TEXTURE0);
                                glBindTexture(GL_TEXTURE_2D, fallbackIt->second.textureId);
                                glUniform1i(glGetUniformLocation(shaderProgram_, "Texture0"), 0);
                            }
                        }
                        
                        // Set base color factor from mesh material
                        GLint colorLoc = glGetUniformLocation(shaderProgram_, "BaseColorFactor");
                        if (colorLoc != -1) {
                            glUniform4f(colorLoc, mesh.baseColor[0], mesh.baseColor[1], mesh.baseColor[2], mesh.baseColor[3]);
                        }

                        if (mvpLoc != -1) {
                            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, meshMVP);
                        }
                        
                        glBindVertexArray(mesh.vao);
                        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_SHORT, 0);
                        glBindVertexArray(0);
                    }
                }
            }
        }
    }
    
    // Render rays
    RenderRays(viewMatrix, projMatrix);

restore_state:
    // Restore ALL GL state to prevent DJUI crash
    // Restore buffer bindings first (before VAO)
    glBindBuffer(GL_ARRAY_BUFFER, prevArrayBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prevElementArrayBuffer);
    
    // Restore VAO
    glBindVertexArray(prevVAO);
    
    // Restore texture state
    glActiveTexture(prevActiveTexture);
    glBindTexture(GL_TEXTURE_2D, prevTexture2D);
    
    // Restore blend function
    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcAlpha, prevBlendDstAlpha);
    
    // Restore other state
    if (!prevDepthTest) glDisable(GL_DEPTH_TEST);
    if (!prevBlend) glDisable(GL_BLEND);
    if (prevCull) glEnable(GL_CULL_FACE);
    if (prevScissor) glDisable(GL_SCISSOR_TEST); // Changed to glDisable as it was glEnable
    glDepthMask(prevDepthMask);
    glUseProgram(prevProgram);
}

void OpenXRKeyboard::Update(XrSpace currentSpace, XrTime predictedDisplayTime, const XrPosef& headPose) {
    if (!initialized_ || keyboardHandle_ == XR_NULL_HANDLE) {
        return;
    }
    
    currentSpace_ = currentSpace;
    currentDisplayTime_ = predictedDisplayTime;
    currentHeadPose_ = headPose;
    
    // Reset ray state for this frame
    leftRay_.active = false;
    rightRay_.active = false;
    
    // If space is not created, create it relative to currentSpace
    if (space_ == XR_NULL_HANDLE && currentSpace != XR_NULL_HANDLE) {
        XrVirtualKeyboardSpaceCreateInfoMETA spaceCreateInfo{XR_TYPE_VIRTUAL_KEYBOARD_SPACE_CREATE_INFO_META};
        spaceCreateInfo.space = currentSpace;
        spaceCreateInfo.poseInSpace = { {0,0,0,1}, {0,0,-1.0f} }; // 1.0m in front
        if (xrCreateVirtualKeyboardSpaceMETA_(session_, keyboardHandle_, &spaceCreateInfo, &space_) != XR_SUCCESS) {
             std::cerr << "Failed to create keyboard space" << std::endl;
        }
    }
    
    // Handle recentering
    if (recenterRequested_ && currentSpace != XR_NULL_HANDLE) {
        const XrQuaternionf& q = headPose.orientation;
        
        float forwardX = 2.0f * (q.x * q.z + q.y * q.w);
        float forwardY = 2.0f * (q.y * q.z - q.x * q.w);
        float forwardZ = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
        
        float horizX = forwardX;
        float horizZ = forwardZ;
        
        float horizLen = std::sqrt(horizX * horizX + horizZ * horizZ);
        if (horizLen > 0.0001f) {
            horizX /= horizLen;
            horizZ /= horizLen;
        }

        float yaw = std::atan2(horizX, -horizZ) + M_PI / 2.0f;

        float halfYaw = yaw * 0.5f;
        float sinHalfYaw = std::sin(halfYaw);
        float cosHalfYaw = std::cos(halfYaw);
        
        XrQuaternionf keyboardOrientation = {0.0f, sinHalfYaw, 0.0f, cosHalfYaw};

        float dist = 1.0f;
        XrVector3f keyboardPosition;
        keyboardPosition.x = headPose.position.x - dist * std::sin(yaw);
        keyboardPosition.y = headPose.position.y - 0.2f; // 20cm below head height
        keyboardPosition.z = headPose.position.z - dist * std::cos(yaw);
    
        XrVirtualKeyboardLocationInfoMETA locationInfo{XR_TYPE_VIRTUAL_KEYBOARD_LOCATION_INFO_META};
        locationInfo.locationType = XR_VIRTUAL_KEYBOARD_LOCATION_TYPE_CUSTOM_META;
        locationInfo.space = currentSpace;
        locationInfo.poseInSpace.position = keyboardPosition;
        locationInfo.poseInSpace.orientation = keyboardOrientation;
        locationInfo.scale = 1.0f;
        
        if (SuggestLocation(&locationInfo)) {
            recenterRequested_ = false;
        }
    }
    
    if (isVisible_ && modelLoaded_) {
        UpdateTextures();
        
        XrVirtualKeyboardModelAnimationStatesMETA animationStates;
        if (GetModelAnimationStates(animationStates)) {
            if (animationStates.stateCountOutput > 0 && animationStates.states != nullptr) {
                UpdateAnimations(animationStates);
                ApplyDirtyMeshUpdates();
            }
        }
    }

}

bool OpenXRKeyboard::SuggestLocation(const XrVirtualKeyboardLocationInfoMETA* locationInfo) {
    if (!initialized_ || keyboardHandle_ == XR_NULL_HANDLE) return false;
    OXR_RET(xrSuggestVirtualKeyboardLocationMETA_(keyboardHandle_, locationInfo));
    return true;
}

bool OpenXRKeyboard::GetLocation(XrSpace baseSpace, XrTime time, VirtualKeyboardLocation* keyboardLocation) {
    if (!initialized_ || keyboardHandle_ == XR_NULL_HANDLE || space_ == XR_NULL_HANDLE) return false;
    
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    OXR_RET(xrLocateSpace(space_, baseSpace, time, &location));
    
    if ((location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0 ||
        (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0) {
        return false;
    }
    
    keyboardLocation->pose = location.pose;
    
    OXR_RET(xrGetVirtualKeyboardScaleMETA_(keyboardHandle_, &keyboardLocation->scale));
    return true;
}


// TODO: probably dont call it twice
bool OpenXRKeyboard::GetModelAnimationStates(XrVirtualKeyboardModelAnimationStatesMETA& modelAnimationStates) {
    if (!initialized_ || keyboardHandle_ == XR_NULL_HANDLE) {
        std::cerr << "GetModelAnimationStates: Not initialized or no keyboard handle" << std::endl;
        return false;
    }
    
    modelAnimationStates = {XR_TYPE_VIRTUAL_KEYBOARD_MODEL_ANIMATION_STATES_META};
    modelAnimationStates.stateCapacityInput = 0;
    modelAnimationStates.states = nullptr;
    
    XrResult result = xrGetVirtualKeyboardModelAnimationStatesMETA_(keyboardHandle_, &modelAnimationStates);
    if (result != XR_SUCCESS) {
        std::cerr << "GetModelAnimationStates: First call failed with error " << result << std::endl;
        return false;
    }
    
    if (modelAnimationStates.stateCountOutput == 0) {
        return true;
    }
    
    // Properly initialize the buffer with correct type field for each element
    animationStatesBuffer_.clear();
    animationStatesBuffer_.resize(modelAnimationStates.stateCountOutput);
    for (auto& state : animationStatesBuffer_) {
        state.type = XR_TYPE_VIRTUAL_KEYBOARD_ANIMATION_STATE_META;
        state.next = nullptr;
        state.animationIndex = 0;
        state.fraction = 0.0f;
    }
    
    modelAnimationStates.stateCapacityInput = modelAnimationStates.stateCountOutput;
    modelAnimationStates.states = animationStatesBuffer_.data();
    
    result = xrGetVirtualKeyboardModelAnimationStatesMETA_(keyboardHandle_, &modelAnimationStates);
    if (result != XR_SUCCESS) {
        std::cerr << "GetModelAnimationStates: Second call failed with error " << result << std::endl;
        return false;
    }
    
    std::cout << "GetModelAnimationStates success" << std::endl;
    return true;
}

bool OpenXRKeyboard::GetDirtyTextures(std::vector<uint64_t>& textureIds) {
    if (!initialized_ || keyboardHandle_ == XR_NULL_HANDLE) return false;
    
    uint32_t count = 0;
    OXR_RET(xrGetVirtualKeyboardDirtyTexturesMETA_(keyboardHandle_, 0, &count, nullptr));
    if (count == 0) return true;
    
    textureIds.resize(count);
    OXR_RET(xrGetVirtualKeyboardDirtyTexturesMETA_(keyboardHandle_, count, &count, textureIds.data()));
    return true;
}

bool OpenXRKeyboard::GetTextureData(uint64_t textureId, XrVirtualKeyboardTextureDataMETA& textureData) {
    if (!initialized_ || keyboardHandle_ == XR_NULL_HANDLE) return false;
    
    textureData = {XR_TYPE_VIRTUAL_KEYBOARD_TEXTURE_DATA_META};
    textureData.bufferCapacityInput = 0;
    OXR_RET(xrGetVirtualKeyboardTextureDataMETA_(keyboardHandle_, textureId, &textureData));
    
    if (textureData.bufferCountOutput == 0) return false;
    
    textureDataBuffer_.resize(textureData.bufferCountOutput);
    textureData.bufferCapacityInput = textureData.bufferCountOutput;
    textureData.buffer = textureDataBuffer_.data();
    
    OXR_RET(xrGetVirtualKeyboardTextureDataMETA_(keyboardHandle_, textureId, &textureData));
    return true;
}

bool OpenXRKeyboard::SendInput(XrSpace space, XrVirtualKeyboardInputSourceMETA source, const XrPosef& pointerPose, bool pressed, XrPosef* interactorRootPose) {
    if (!initialized_ || keyboardHandle_ == XR_NULL_HANDLE) return false;
    if (!xrSendVirtualKeyboardInputMETA_) {
        std::cerr << "xrSendVirtualKeyboardInputMETA_ function pointer is null!" << std::endl;
        return false;
    }
    
    // Update ray state
    if (source == XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_LEFT_META) {
        leftRay_.active = true;
        leftRay_.pose = pointerPose;
    } else if (source == XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_RIGHT_META) {
        rightRay_.active = true;
        rightRay_.pose = pointerPose;
    }

    XrVirtualKeyboardInputInfoMETA info{XR_TYPE_VIRTUAL_KEYBOARD_INPUT_INFO_META};
    info.inputSource = source;
    info.inputSpace = space;
    info.inputPoseInSpace = pointerPose;
    if (pressed) {
        info.inputState |= XR_VIRTUAL_KEYBOARD_INPUT_STATE_PRESSED_BIT_META;
    }
    OXR_RET(xrSendVirtualKeyboardInputMETA_(keyboardHandle_, &info, interactorRootPose));
    
    // Store interactor root pose if available
    if (interactorRootPose) {
        if (source == XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_LEFT_META) {
            leftRay_.interactorRootPose = *interactorRootPose;
        } else if (source == XR_VIRTUAL_KEYBOARD_INPUT_SOURCE_CONTROLLER_RAY_RIGHT_META) {
            rightRay_.interactorRootPose = *interactorRootPose;
        }
    }
    
    return true;
}

bool OpenXRKeyboard::UpdateTextContext(const std::string& textContext) {
    if (!initialized_ || keyboardHandle_ == XR_NULL_HANDLE) return false;
    
    XrVirtualKeyboardTextContextChangeInfoMETA changeInfo{XR_TYPE_VIRTUAL_KEYBOARD_TEXT_CONTEXT_CHANGE_INFO_META};
    changeInfo.textContext = textContext.c_str();
    OXR_RET(xrChangeVirtualKeyboardTextContextMETA_(keyboardHandle_, &changeInfo));
    return true;
}

extern "C" {
    void openxr_show_keyboard(void) {
        OpenXRKeyboard::GetInstance().ShowKeyboard();
    }
    void openxr_hide_keyboard(void) {
        OpenXRKeyboard::GetInstance().HideKeyboard();
    }
    int openxr_is_keyboard_visible(void) {
        return OpenXRKeyboard::GetInstance().IsVisible() ? 1 : 0;
    }
    void openxr_render_keyboard(int eye) {
        OpenXRKeyboard::GetInstance().Render(eye);
    }
}
// Helper method implementations

bool OpenXRKeyboard::InitRenderModelExtension() {
    // Try to load render model extension functions
    XrResult result;
    
    result = xrGetInstanceProcAddr(instance_, "xrEnumerateRenderModelPathsFB", (PFN_xrVoidFunction*)&xrEnumerateRenderModelPathsFB_);
    if (result != XR_SUCCESS) return false;
    
    result = xrGetInstanceProcAddr(instance_, "xrGetRenderModelPropertiesFB", (PFN_xrVoidFunction*)&xrGetRenderModelPropertiesFB_);
    if (result != XR_SUCCESS) return false;
    
    result = xrGetInstanceProcAddr(instance_, "xrLoadRenderModelFB", (PFN_xrVoidFunction*)&xrLoadRenderModelFB_);
    if (result != XR_SUCCESS) return false;
    
    std::cout << "Render model extension loaded successfully" << std::endl;
    return true;
}

bool OpenXRKeyboard::LoadKeyboardModel() {
    if (!xrEnumerateRenderModelPathsFB_ || !xrGetRenderModelPropertiesFB_ || !xrLoadRenderModelFB_) {
        std::cerr << "Render model extension not available" << std::endl;
        return false;
    }
    
    std::cout << "Loading keyboard model..." << std::endl;
    
    // IMPORTANT: Must enumerate render model paths before querying properties
    // This is required by the OpenXR spec
    uint32_t pathCount = 0;
    XrResult result = xrEnumerateRenderModelPathsFB_(session_, 0, &pathCount, nullptr);
    if (result != XR_SUCCESS) {
        std::cerr << "Failed to enumerate render model paths, error: " << result << std::endl;
        return false;
    }
    
    if (pathCount == 0) {
        std::cerr << "No render models available" << std::endl;
        return false;
    }
    
    // Get all available model paths
    std::vector<XrRenderModelPathInfoFB> pathInfos(pathCount, {XR_TYPE_RENDER_MODEL_PATH_INFO_FB});
    result = xrEnumerateRenderModelPathsFB_(session_, pathCount, &pathCount, pathInfos.data());
    if (result != XR_SUCCESS) {
        std::cerr << "Failed to get render model path infos, error: " << result << std::endl;
        return false;
    }
    
    // Find the keyboard model
    const char* keyboardModelPath = "/model_meta/keyboard/virtual";
    XrPath keyboardXrPath = XR_NULL_PATH;
    
    for (const auto& pathInfo : pathInfos) {
        char pathString[XR_MAX_PATH_LENGTH];
        uint32_t stringLength = 0;
        result = xrPathToString(instance_, pathInfo.path, XR_MAX_PATH_LENGTH, &stringLength, pathString);
        if (result == XR_SUCCESS) {
            if (strcmp(pathString, keyboardModelPath) == 0) {
                keyboardXrPath = pathInfo.path;
            }
        }
    }
    
    if (keyboardXrPath == XR_NULL_PATH) {
        std::cerr << "Keyboard model path not found in available models" << std::endl;
        std::cerr << "Expected path: " << keyboardModelPath << std::endl;
        return false;
    }
    
    // Get model properties
    XrRenderModelPropertiesFB prop{XR_TYPE_RENDER_MODEL_PROPERTIES_FB};
    XrRenderModelCapabilitiesRequestFB capReq{XR_TYPE_RENDER_MODEL_CAPABILITIES_REQUEST_FB};
    capReq.flags = XR_RENDER_MODEL_SUPPORTS_GLTF_2_0_SUBSET_2_BIT_FB;
    prop.next = &capReq;
    
    result = xrGetRenderModelPropertiesFB_(session_, keyboardXrPath, &prop);
    if (result != XR_SUCCESS) {
        std::cerr << "Failed to get keyboard model properties, error code: " << result << std::endl;
        return false;
    }
    
    modelKey_ = prop.modelKey;
    if (modelKey_ == XR_NULL_RENDER_MODEL_KEY_FB) {
        std::cerr << "Model key is null - model may not be available yet" << std::endl;
        return false;
    }
    
    std::cout << "Got model key: " << modelKey_ << std::endl;
    std::cout << "Model name: " << prop.modelName << std::endl;
    std::cout << "Model version: " << prop.modelVersion << std::endl;
    
    // Load the model data
    XrRenderModelLoadInfoFB loadInfo{XR_TYPE_RENDER_MODEL_LOAD_INFO_FB};
    loadInfo.modelKey = modelKey_;
    
    XrRenderModelBufferFB rmb{XR_TYPE_RENDER_MODEL_BUFFER_FB};
    OXR_RET(xrLoadRenderModelFB_(session_, &loadInfo, &rmb));
    
    modelDataBuffer_.resize(rmb.bufferCountOutput);
    rmb.buffer = modelDataBuffer_.data();
    rmb.bufferCapacityInput = rmb.bufferCountOutput;
    
    OXR_RET(xrLoadRenderModelFB_(session_, &loadInfo, &rmb));

    if (!ParseGLTFModel(modelDataBuffer_.data(), modelDataBuffer_.size())) {
        std::cerr << "Failed to parse keyboard model" << std::endl;
        return false;
    }
    
    if (!CreateShaderProgram()) {
        std::cerr << "Failed to create shader program" << std::endl;
        return false;
    }
    
    modelLoaded_ = true;
    std::cout << "Keyboard model loaded successfully" << std::endl;
    return true;
}

void OpenXRKeyboard::UnloadKeyboardModel() {
    CleanupGL();
    modelDataBuffer_.clear();
    modelLoaded_ = false;
    modelKey_ = XR_NULL_RENDER_MODEL_KEY_FB;
}

bool OpenXRKeyboard::CreateShaderProgram() {
    const char* vertexShaderSrc = R"(
        #version 300 es
        precision highp float;
        
        layout(location = 0) in vec3 Position;
        layout(location = 1) in vec2 TexCoord;
        
        out lowp vec2 oTexCoord;
        
        uniform mat4 uModelViewProjection;
        
        void main() {
            gl_Position = uModelViewProjection * vec4(Position, 1.0);
            oTexCoord = TexCoord;
        }
    )";
    
    const char* fragmentShaderSrc = R"(
        #version 300 es
        precision lowp float;
        
        in lowp vec2 oTexCoord;
        out vec4 fragColor;
        
        uniform sampler2D Texture0;
        uniform lowp vec4 BaseColorFactor;
        
        void main() {
            lowp vec4 diffuse = texture(Texture0, oTexCoord);
            lowp vec3 finalColor = diffuse.rgb * BaseColorFactor.rgb;
            
            float alpha = diffuse.a;
            
            fragColor.rgb = finalColor;
            fragColor.a = alpha;
        }
    )";
    
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSrc, nullptr);
    glCompileShader(vertexShader);
    
    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        std::cerr << "Vertex shader compilation failed: " << infoLog << std::endl;
        return false;
    }
    
    // Compile fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSrc, nullptr);
    glCompileShader(fragmentShader);
    
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        std::cerr << "Fragment shader compilation failed: " << infoLog << std::endl;
        glDeleteShader(vertexShader);
        return false;
    }
    
    // Link shader program
    shaderProgram_ = glCreateProgram();
    glAttachShader(shaderProgram_, vertexShader);
    glAttachShader(shaderProgram_, fragmentShader);
    glLinkProgram(shaderProgram_);
    
    glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(shaderProgram_, 512, nullptr, infoLog);
        std::cerr << "Shader program linking failed: " << infoLog << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    return true;
}

bool OpenXRKeyboard::ParseGLTFModel(const uint8_t* data, size_t size) {
    gltfModel_ = {};
    if (!KeyboardGLTF::ParseGLB(data, size, gltfModel_)) {
        std::cerr << "Failed to parse GLB data" << std::endl;
        return false;
    }
    
    meshes_.clear();
    
    std::map<int, int> nodeToMeshIndex;
    
    for (size_t nodeIdx = 0; nodeIdx < gltfModel_.nodes.size(); nodeIdx++) {
        const auto& node = gltfModel_.nodes[nodeIdx];
        
        // Skip nodes without a model
        if (node.model == nullptr || node.model->surfaces.empty()) {
            continue;
        }
        
        // Create VBO ONLY for surfaces[0] (the active surface)
        const auto& surface = node.model->surfaces[0];
        
        KeyboardMesh mesh;
        mesh.vertexCount = surface.vertices.size();
        mesh.indexCount = surface.indices.size();
        mesh.textureId = surface.textureId;
        mesh.nodeIndex = nodeIdx;  // Store which node this mesh belongs to
        
        // Copy base color
        mesh.baseColor[0] = surface.baseColor.x;
        mesh.baseColor[1] = surface.baseColor.y;
        mesh.baseColor[2] = surface.baseColor.z;
        mesh.baseColor[3] = surface.baseColor.w;
        
        // Create VAO
        glGenVertexArrays(1, &mesh.vao);
        glBindVertexArray(mesh.vao);
        
        // Create VBO
        glGenBuffers(1, &mesh.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, surface.vertices.size() * sizeof(KeyboardGLTF::Vertex), 
                     surface.vertices.data(), GL_DYNAMIC_DRAW);
        
        // Create EBO
        glGenBuffers(1, &mesh.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, surface.indices.size() * sizeof(uint16_t), 
                     surface.indices.data(), GL_STATIC_DRAW);
        
        // Set vertex attributes
        // Position (Vec3)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(KeyboardGLTF::Vertex), 
                              (void*)offsetof(KeyboardGLTF::Vertex, position));
        
        // TexCoord (Vec2)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(KeyboardGLTF::Vertex), 
                              (void*)offsetof(KeyboardGLTF::Vertex, texCoord));
        
        glBindVertexArray(0);
        
        nodeToMeshIndex[nodeIdx] = meshes_.size();
        
        meshes_.push_back(mesh);
    }
    
    // Initialize dirty mesh tracking
    dirtyMeshes_.resize(meshes_.size(), false);
    
    // TODO: probably remove this since textures are fixed now
    // Create a fallback checkerboard texture for meshes without materials (texture ID 0)
    {
        KeyboardTexture fallbackTex;
        fallbackTex.width = 64;
        fallbackTex.height = 64;
        glGenTextures(1, &fallbackTex.textureId);
        glBindTexture(GL_TEXTURE_2D, fallbackTex.textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        // Create transparent fallback texture (ID 0)
        std::vector<uint8_t> transparent(fallbackTex.width * fallbackTex.height * 4, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fallbackTex.width, fallbackTex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, transparent.data());
        
        std::cout << "Created fallback transparent texture 0 (GL ID: " << fallbackTex.textureId << ") for meshes without materials" << std::endl;
        textures_[0] = fallbackTex;
    }
    
    // TODO: probably remove this since textures are fixed now
    // Pre-populate textures map with checkerboard placeholders or embedded textures
    for (const auto& texInfo : gltfModel_.textures) {
        if (textures_.find(texInfo.id) == textures_.end()) {
            KeyboardTexture tex;
            tex.width = texInfo.width;
            tex.height = texInfo.height;
            glGenTextures(1, &tex.textureId);
            glBindTexture(GL_TEXTURE_2D, tex.textureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            
            if (!texInfo.data.empty()) {
                // Load embedded texture data
                // Assume RGBA for now, tinygltf usually provides RGBA
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texInfo.data.data());
                std::cout << "glTF: Loaded embedded texture " << texInfo.id << " (GL ID: " << tex.textureId << ")" << std::endl;
            } else {
                // Create a transparent placeholder texture
                // This prevents seeing a checkerboard before the actual texture loads
                std::vector<uint8_t> transparent(tex.width * tex.height * 4, 0);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, transparent.data());
                
                std::cout << "Created placeholder transparent texture " << texInfo.id << " (GL ID: " << tex.textureId 
                          << ") size: " << tex.width << "x" << tex.height << std::endl;
            }
            
            textures_[texInfo.id] = tex;
        }
    }
    
    // Generate runtime state from the static model
    modelState_.GenerateFromModel(gltfModel_);
    
    modelState_.nodeToFirstMesh = nodeToMeshIndex;
    
    // TODO: probably not needed at this stage
    modelState_.RecalculateGlobalTransforms();
    
    return true;
}

void KeyboardModelState::GenerateFromModel(const KeyboardGLTF::Model& model) {
    nodeStates.clear();
    
    if (model.nodes.empty()) {
        std::cerr << "GenerateFromModel: ERROR - model has no nodes!" << std::endl;
        return;
    }
    
    nodeStates.reserve(model.nodes.size());
    
    for (size_t nodeIdx = 0; nodeIdx < model.nodes.size(); nodeIdx++) {
        const auto& node = model.nodes[nodeIdx];
        
        KeyboardNodeState nodeState;
        
        nodeState.node = &model.nodes[nodeIdx];
        
        if (node.translation.size() == 3) {
            nodeState.translation[0] = (float)node.translation[0];
            nodeState.translation[1] = (float)node.translation[1];
            nodeState.translation[2] = (float)node.translation[2];
            nodeState.initialTranslation[0] = (float)node.translation[0];
            nodeState.initialTranslation[1] = (float)node.translation[1];
            nodeState.initialTranslation[2] = (float)node.translation[2];
        }
        
        if (node.rotation.size() == 4) {
            nodeState.rotation[0] = (float)node.rotation[0];
            nodeState.rotation[1] = (float)node.rotation[1];
            nodeState.rotation[2] = (float)node.rotation[2];
            nodeState.rotation[3] = (float)node.rotation[3];
            nodeState.initialRotation[0] = (float)node.rotation[0];
            nodeState.initialRotation[1] = (float)node.rotation[1];
            nodeState.initialRotation[2] = (float)node.rotation[2];
            nodeState.initialRotation[3] = (float)node.rotation[3];
        }
        
        if (node.scale.size() == 3) {
            nodeState.scale[0] = (float)node.scale[0];
            nodeState.scale[1] = (float)node.scale[1];
            nodeState.scale[2] = (float)node.scale[2];
            nodeState.initialScale[0] = (float)node.scale[0];
            nodeState.initialScale[1] = (float)node.scale[1];
            nodeState.initialScale[2] = (float)node.scale[2];
        }
        
        if (node.model != nullptr && !node.model->weights.empty()) {
            nodeState.weights = node.model->weights;
        }
        
        nodeState.CalculateLocalTransform();
        
        memcpy(nodeState.globalTransform, nodeState.localTransform, sizeof(float) * 16);
        
        nodeStates.push_back(nodeState);
    }

    std::cout << "ModelState: Generated " << nodeStates.size() << " node states" << std::endl;
}

void KeyboardModelState::RecalculateGlobalTransforms() {
    if (nodeStates.empty()) {
        std::cout << "RecalculateGlobalTransforms: No node states, skipping" << std::endl;
        return;
    }
    
    std::vector<std::vector<int>> children(nodeStates.size());
    std::vector<int> roots;
    
    for (size_t i = 0; i < nodeStates.size(); i++) {
        if (!nodeStates[i].node) continue;  // Skip if no node pointer
        int p = nodeStates[i].node->parentIndex;
        if (p >= 0 && p < nodeStates.size()) {
            children[p].push_back(i);
        } else {
            roots.push_back(i);
        }
    }
    
    // Define the recursive function properly to capture 'children'
    std::function<void(int, const float*)> traverse;
    traverse = [&](int nodeIdx, const float* parentGlobalTransform) {
        auto& nodeState = nodeStates[nodeIdx];
        
        if (parentGlobalTransform) {
            MatrixMultiply(parentGlobalTransform, nodeState.localTransform, nodeState.globalTransform);
        } else {
            // Root node, global transform is its local transform
            memcpy(nodeState.globalTransform, nodeState.localTransform, sizeof(float) * 16);
        }
        
        for (int childIdx : children[nodeIdx]) {
            traverse(childIdx, nodeState.globalTransform);
        }
    };
    
    // Start traversal from roots
    for (int rootIdx : roots) {
        traverse(rootIdx, nullptr);
    }
}

// ============================================================================
// KeyboardNodeState Method Implementations
// ============================================================================

void KeyboardNodeState::CalculateLocalTransform() {
    // Create vectors for MatrixFromTRS helper
    std::vector<double> t = {translation[0], translation[1], translation[2]};
    std::vector<double> r = {rotation[0], rotation[1], rotation[2], rotation[3]};
    std::vector<double> s = {scale[0], scale[1], scale[2]};
    
    KeyboardGLTF::MatrixFromTRS(t, r, s, localTransform);
}

void OpenXRKeyboard::UpdateTextures() {
    if (!initialized_ || keyboardHandle_ == XR_NULL_HANDLE) return;
    
    // Set the keyboard model visibility to update internal state and mark dirty textures
    XrVirtualKeyboardModelVisibilitySetInfoMETA visibility = {XR_TYPE_VIRTUAL_KEYBOARD_MODEL_VISIBILITY_SET_INFO_META};
    visibility.visible = XR_TRUE;
    XrResult result = xrSetVirtualKeyboardModelVisibilityMETA_(keyboardHandle_, &visibility);
    if (result != XR_SUCCESS) {
        std::cerr << "Failed to sync keyboard model, error: " << result << std::endl;
        return;
    }
    
    std::vector<uint64_t> textureIds;
    
    // Force update all textures on first load
    if (!texturesLoaded_) {
        std::cout << "First texture update: Forcing update of all " << textures_.size() << " textures" << std::endl;
        for (const auto& pair : textures_) {
            textureIds.push_back(pair.first);
        }
        texturesLoaded_ = true;
    } else {
        // Get dirty textures normally
        if (!GetDirtyTextures(textureIds)) return;
    }
    
    if (!textureIds.empty()) {
        std::cout << "Keyboard: " << textureIds.size() << " dirty textures to update" << std::endl;
    }
    
    // Update each dirty texture
    for (uint64_t textureId : textureIds) {
        XrVirtualKeyboardTextureDataMETA textureData;
        
        if (GetTextureData(textureId, textureData)) {
            auto it = textures_.find(textureId);
            if (it == textures_.end()) {
                KeyboardTexture tex;
                glGenTextures(1, &tex.textureId);
                glBindTexture(GL_TEXTURE_2D, tex.textureId);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                    textureData.textureWidth, textureData.textureHeight,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, textureData.buffer);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                tex.width = textureData.textureWidth;
                tex.height = textureData.textureHeight;
                textures_[textureId] = tex;
            } else {
                // Update existing texture with data directly (testing without BGRA swap)
                glBindTexture(GL_TEXTURE_2D, it->second.textureId);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 
                    textureData.textureWidth, textureData.textureHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE, textureData.buffer);
                
                // Set wrapping parameters to prevent black edges
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
        } else {
            std::cerr << "    Failed to get texture data for ID: " << textureId << std::endl;
        }
    }
}

void OpenXRKeyboard::CleanupGL() {
    // Delete shader program
    if (shaderProgram_ != 0) {
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
    }
    
    // Delete meshes
    for (auto& mesh : meshes_) {
        if (mesh.vao != 0) glDeleteVertexArrays(1, &mesh.vao);
        if (mesh.vbo != 0) glDeleteBuffers(1, &mesh.vbo);
        if (mesh.ebo != 0) glDeleteBuffers(1, &mesh.ebo);
    }
    meshes_.clear();
    
    // Delete textures
    for (auto& pair : textures_) {
        if (pair.second.textureId != 0) {
            glDeleteTextures(1, &pair.second.textureId);
        }
    }
    textures_.clear();
    
    // Cleanup ray resources
    if (rayShaderProgram_ != 0) {
        glDeleteProgram(rayShaderProgram_);
        rayShaderProgram_ = 0;
    }
    if (rayVao_ != 0) glDeleteVertexArrays(1, &rayVao_);
    if (rayVbo_ != 0) glDeleteBuffers(1, &rayVbo_);
}

bool OpenXRKeyboard::CreateRayResources() {
    if (rayShaderProgram_ != 0) return true;

    const char* vertexShaderSrc = R"(
        #version 300 es
        precision highp float;
        layout(location = 0) in vec3 aPosition;
        uniform mat4 uModelViewProjection;
        void main() {
            gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
        }
    )";

    const char* fragmentShaderSrc = R"(
        #version 300 es
        precision mediump float;
        out vec4 fragColor;
        uniform vec4 uColor;
        void main() {
            fragColor = uColor;
        }
    )";

    // Compile shaders
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSrc, nullptr);
    glCompileShader(vertexShader);
    
    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        std::cerr << "Ray vertex shader compilation failed: " << infoLog << std::endl;
        return false;
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSrc, nullptr);
    glCompileShader(fragmentShader);
    
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        std::cerr << "Ray fragment shader compilation failed: " << infoLog << std::endl;
        glDeleteShader(vertexShader);
        return false;
    }

    rayShaderProgram_ = glCreateProgram();
    glAttachShader(rayShaderProgram_, vertexShader);
    glAttachShader(rayShaderProgram_, fragmentShader);
    glLinkProgram(rayShaderProgram_);
    
    glGetProgramiv(rayShaderProgram_, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(rayShaderProgram_, 512, nullptr, infoLog);
        std::cerr << "Ray shader program linking failed: " << infoLog << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Create ray mesh (line)
    // Ray points forward along -Z axis (OpenXR convention)
    // Start at origin, extend 2 meters forward
    float vertices[] = {
        0.0f, 0.0f, 0.0f,    // Ray origin
        0.0f, 0.0f, -2.0f    // Ray end (2m forward along -Z)
    };

    glGenVertexArrays(1, &rayVao_);
    glBindVertexArray(rayVao_);

    glGenBuffers(1, &rayVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, rayVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    
    return true;
}

void OpenXRKeyboard::RenderRays(const float* viewMatrix, const float* projMatrix) {
    if (rayShaderProgram_ == 0) {
        if (!CreateRayResources()) return;
    }

    // TODO: is this necessary? Save previous line width
    GLfloat prevLineWidth;
    glGetFloatv(GL_LINE_WIDTH, &prevLineWidth);
    glLineWidth(3.0f); // Make rays more visible

    glUseProgram(rayShaderProgram_);

    auto renderRay = [&](const RayState& ray, const char* name, float r, float g, float b) {
        if (!ray.active) return;

        float modelMatrix[16];
        ComposeModelMatrix(ray.pose, 1.0f, modelMatrix);

        float viewModelMatrix[16];
        MultiplyMatrix(viewMatrix, modelMatrix, viewModelMatrix);

        float mvpMatrix[16];
        MultiplyMatrix(projMatrix, viewModelMatrix, mvpMatrix);

        GLint mvpLoc = glGetUniformLocation(rayShaderProgram_, "uModelViewProjection");
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvpMatrix);

        GLint colorLoc = glGetUniformLocation(rayShaderProgram_, "uColor");
        glUniform4f(colorLoc, r, g, b, 0.8f); // Use specified color with high alpha

        glBindVertexArray(rayVao_);
        glDrawArrays(GL_LINES, 0, 2);
        glBindVertexArray(0);
    };

    // Left ray = Blue, Right ray = Red
    renderRay(leftRay_, "Left", 0.0f, 0.5f, 1.0f);   // Blue
    renderRay(rightRay_, "Right", 1.0f, 0.5f, 0.0f); // Orange/Red
    
    // Restore previous line width
    glLineWidth(prevLineWidth);
}

// ============================================================================
// Animation Interpolation Helpers (based on Meta SDK implementation)
// ============================================================================

// Linear interpolation for Vec3 (used for translation and scale)
static void InterpolateVec3(
    const float* buffer,
    int frame,
    float fraction,
    float* outVec3) {
    
    // First element at frame
    float x0 = buffer[frame * 3 + 0];
    float y0 = buffer[frame * 3 + 1];
    float z0 = buffer[frame * 3 + 2];
    
    // Second element at frame + 1
    float x1 = buffer[frame * 3 + 3];
    float y1 = buffer[frame * 3 + 4];
    float z1 = buffer[frame * 3 + 5];
    
    // Linear interpolation
    outVec3[0] = x0 + (x1 - x0) * fraction;
    outVec3[1] = y0 + (y1 - y0) * fraction;
    outVec3[2] = z0 + (z1 - z0) * fraction;
}

// Spherical linear interpolation for quaternions (used for rotation)
static void InterpolateQuat(
    const float* buffer,
    int frame,
    float fraction,
    float* outQuat) {
    
    // First quaternion at frame (x, y, z, w)
    float x0 = buffer[frame * 4 + 0];
    float y0 = buffer[frame * 4 + 1];
    float z0 = buffer[frame * 4 + 2];
    float w0 = buffer[frame * 4 + 3];
    
    // Second quaternion at frame + 1
    float x1 = buffer[frame * 4 + 4];
    float y1 = buffer[frame * 4 + 5];
    float z1 = buffer[frame * 4 + 6];
    float w1 = buffer[frame * 4 + 7];
    
    // Simple linear interpolation (normalized lerp)
    // For keyboard animations, this is sufficient
    outQuat[0] = x0 + (x1 - x0) * fraction;
    outQuat[1] = y0 + (y1 - y0) * fraction;
    outQuat[2] = z0 + (z1 - z0) * fraction;
    outQuat[3] = w0 + (w1 - w0) * fraction;
    
    // Normalize the result
    float len = std::sqrt(
        outQuat[0] * outQuat[0] +
        outQuat[1] * outQuat[1] +
        outQuat[2] * outQuat[2] +
        outQuat[3] * outQuat[3]
    );
    
    if (len > 0.0001f) {
        outQuat[0] /= len;
        outQuat[1] /= len;
        outQuat[2] /= len;
        outQuat[3] /= len;
    }
}

void OpenXRKeyboard::UpdateAnimations(const XrVirtualKeyboardModelAnimationStatesMETA& animationStates) {
    // Safety check: ensure states pointer is valid
    if (animationStates.states == nullptr) {
        std::cerr << "UpdateAnimations: states pointer is null" << std::endl;
        return;
    }
    
    // Safety check: ensure we have animations in the model
    if (gltfModel_.animations.empty()) {
        std::cout << "UpdateAnimations: No animations in glTF model" << std::endl;
        return;
    }
    
    // Ensure dirty mesh tracking is initialized
    if (dirtyMeshes_.size() != meshes_.size()) {
        dirtyMeshes_.resize(meshes_.size(), false);
    }

    // Track which nodes have been modified by animations
    std::vector<int> affectedNodeIndices;
    
    // Map animation states to glTF animations
    for (uint32_t i = 0; i < animationStates.stateCountOutput; i++) {
        const auto& state = animationStates.states[i];
        
        // Validate animation index BEFORE accessing
        if (state.animationIndex >= gltfModel_.animations.size()) {
            std::cout << "  Invalid animation index: " << state.animationIndex 
                      << " (max: " << gltfModel_.animations.size() << ")" << std::endl;
            continue;
        }
        
        const auto& anim = gltfModel_.animations[state.animationIndex];
        float time = state.fraction; 
        
        // Calculate animation duration from samplers
        float maxTime = 0.0f;
        for (const auto& sampler : anim.samplers) {
            if (!sampler.input.empty()) {
                maxTime = std::max(maxTime, sampler.input.back());
            }
        }
        
        float animTime = time * maxTime;
        
        // Apply animation to channels
        for (const auto& channel : anim.channels) {
            const auto& sampler = anim.samplers[channel.samplerIndex];
            if (sampler.input.empty() || sampler.output.empty()) continue;
            
            // Find keyframes
            size_t nextIdx = 0;
            while (nextIdx < sampler.input.size() && sampler.input[nextIdx] < animTime) {
                nextIdx++;
            }
            
            size_t prevIdx = (nextIdx > 0) ? nextIdx - 1 : 0;
            if (nextIdx >= sampler.input.size()) nextIdx = sampler.input.size() - 1;
            
            float t0 = sampler.input[prevIdx];
            float t1 = sampler.input[nextIdx];
            float factor = 0.0f;
            if (t1 > t0) {
                factor = (animTime - t0) / (t1 - t0);
            }
            
            // Find the target node
            if (channel.nodeIndex < 0 || channel.nodeIndex >= gltfModel_.nodes.size()) {
                std::cout << "    Invalid node index: " << channel.nodeIndex << std::endl;
                continue;
            }
            
            // Get the node state directly by index
            if (channel.nodeIndex >= modelState_.nodeStates.size()) {
                std::cout << "    Node state index out of range: " << channel.nodeIndex << std::endl;
                continue;
            }
            
            KeyboardNodeState* nodeState = &modelState_.nodeStates[channel.nodeIndex];
            
            if (nodeState == nullptr || nodeState->node == nullptr) {
                std::cout << "    No node state found for node " << channel.nodeIndex << std::endl;
                continue;
            }
            
            // Handle different animation channel types
            if (channel.path == "translation") {
                // Translation channel - interpolate Vec3
                float translation[3];
                InterpolateVec3(sampler.output.data(), prevIdx, factor, translation);
                nodeState->translation[0] = translation[0];
                nodeState->translation[1] = translation[1];
                nodeState->translation[2] = translation[2];
                
                // Track this node for transform recalculation
                if (std::find(affectedNodeIndices.begin(), affectedNodeIndices.end(), channel.nodeIndex) == affectedNodeIndices.end()) {
                    affectedNodeIndices.push_back(channel.nodeIndex);
                }
            } else if (channel.path == "scale") {
                // Scale channel - interpolate Vec3
                float scale[3];
                InterpolateVec3(sampler.output.data(), prevIdx, factor, scale);
                nodeState->scale[0] = scale[0];
                nodeState->scale[1] = scale[1];
                nodeState->scale[2] = scale[2];
                
                // Track this node for transform recalculation
                if (std::find(affectedNodeIndices.begin(), affectedNodeIndices.end(), channel.nodeIndex) == affectedNodeIndices.end()) {
                    affectedNodeIndices.push_back(channel.nodeIndex);
                }
                
            } else if (channel.path == "rotation") {
                // Rotation channel - interpolate Quaternion
                float rotation[4];
                InterpolateQuat(sampler.output.data(), prevIdx, factor, rotation);
                nodeState->rotation[0] = rotation[0];
                nodeState->rotation[1] = rotation[1];
                nodeState->rotation[2] = rotation[2];
                nodeState->rotation[3] = rotation[3];
                
                // Track this node for transform recalculation
                if (std::find(affectedNodeIndices.begin(), affectedNodeIndices.end(), channel.nodeIndex) == affectedNodeIndices.end()) {
                    affectedNodeIndices.push_back(channel.nodeIndex);
                }
                
            } else if (channel.path == "weights") {
                // Weights channel - morph target animation
                
                // Get node state
                if (channel.nodeIndex >= modelState_.nodeStates.size()) continue;
                auto& nodeState = modelState_.nodeStates[channel.nodeIndex];
                
                // Check if node has a model with surfaces
                if (nodeState.node == nullptr || nodeState.node->model == nullptr) {
                    std::cout << "    Node has no model" << std::endl;
                    continue;
                }
                if (nodeState.node->model->surfaces.empty()) {
                    std::cout << "    Node model has no surfaces" << std::endl;
                    continue;
                }
                
                // Access surface (Meta's pattern: use surfaces[0])
                const auto& surface = nodeState.node->model->surfaces[0];
                size_t numWeights = surface.morphTargets.size();
                if (numWeights == 0) {
                    continue;
                }
                
                // Check if this is an additive animation (targets a specific weight)
                // or a full animation (replaces all weights)
                int additiveIdx = channel.additiveWeightIndex;
                
                if (additiveIdx >= 0) {
                    // ADDITIVE MODE: Update only the specified morph target weight  
                    if (additiveIdx < numWeights && additiveIdx < nodeState.weights.size()) {
                        // Use the animation state's fraction directly as the weight value
                        float weight = state.fraction;
                        
                        nodeState.weights[additiveIdx] += weight;

                    }
                } else {
                    // DEFAULT MODE: REPLACE all weights
                    for (size_t w = 0; w < numWeights; w++) {
                        size_t idx0 = prevIdx * numWeights + w;
                        size_t idx1 = nextIdx * numWeights + w;
                        
                        if (idx1 < sampler.output.size()) {
                            float v0 = sampler.output[idx0];
                            float v1 = sampler.output[idx1];
                            float weight = v0 + (v1 - v0) * factor;
                            
                            if (w < nodeState.weights.size()) {
                                nodeState.weights[w] = weight;
                            }
                        }
                    }
                }
                
                // Mark mesh as dirty for VBO update
                auto meshIt2 = modelState_.nodeToFirstMesh.find(channel.nodeIndex);
                if (meshIt2 != modelState_.nodeToFirstMesh.end()) {
                    MarkMeshDirty(meshIt2->second);
                }
                
            } else {
                // Unknown channel type
                std::cout << "    Unknown animation channel path: " << channel.path << std::endl;
            }
        }
    }
    

    // TODO: I have no idea why this works, but it is needed other wise the keyboard
    // will not render in global space, keeping it for now until another solution is found
    // WORKAROUND: Fix malformed scale animations
    // Many nodes have animations setting scale to patterns like (1,0,1) or (1,0,0)
    // which makes them invisible due to zero components. This appears to be bad
    // animation data in the glTF, as initial values are correct (1,1,1).
    // Fix: If X=1 and Y=0, assume it should be (1,1,1)
    for (int nodeIndex : affectedNodeIndices) {
        if (nodeIndex < modelState_.nodeStates.size()) {
            auto& ns = modelState_.nodeStates[nodeIndex];
            // Detect pattern: X=1, Y=0 -> probably should be (1,1,1)
            if (ns.scale[0] == 1.0f && ns.scale[1] == 0.0f) {
                ns.scale[1] = 1.0f;
                if (ns.scale[2] == 0.0f) {
                    ns.scale[2] = 1.0f;
                }
            }
        }
    }
    
    // After all channels have been processed, recalculate transforms for affected nodes
    // This matches Meta SDK's pattern: apply all channel values, then recalculate matrices
    for (int nodeIndex : affectedNodeIndices) {
        // Find node state by comparing with array index
        for (size_t i = 0; i < modelState_.nodeStates.size(); i++) {
            if (i == nodeIndex) {
                modelState_.nodeStates[i].CalculateLocalTransform();
                break;
            }
        }
    }
    
    // Recalculate global transforms for the entire hierarchy
    modelState_.RecalculateGlobalTransforms();
}

void OpenXRKeyboard::MarkMeshDirty(size_t meshIndex) {
    if (meshIndex < dirtyMeshes_.size()) {
        dirtyMeshes_[meshIndex] = true;
    }
}

void OpenXRKeyboard::ApplyDirtyMeshUpdates() {
    // Save GL state
    GLint prevArrayBuffer;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    
    int activeMorphCount = 0;
    
    // Apply morphed vertices to dirty meshes
    for (size_t nodeIdx = 0; nodeIdx < modelState_.nodeStates.size(); nodeIdx++) {
        auto& nodeState = modelState_.nodeStates[nodeIdx];
        
        // Skip nodes without weights or model
        if (nodeState.weights.empty()) continue;
        if (nodeState.node == nullptr || nodeState.node->model == nullptr) continue;
        if (nodeState.node->model->surfaces.empty()) continue;
        
        // Get mesh index from mapping
        auto meshIt = modelState_.nodeToFirstMesh.find(nodeIdx);
        if (meshIt == modelState_.nodeToFirstMesh.end()) continue;
        
        int meshIdx = meshIt->second;
        
        // Check if this mesh is dirty
        if (meshIdx < 0 || meshIdx >= dirtyMeshes_.size() || !dirtyMeshes_[meshIdx]) {
            continue;
        }
        
        // Access surface from node->model->surfaces[0] (Meta's pattern)
        auto& surface = nodeState.node->model->surfaces[0];
        
        // Compute morphed vertices
        // Start with original vertices from surface
        std::vector<KeyboardGLTF::Vertex> morphedVertices = surface.originalVertices;
        
        // Apply morph targets
        for (size_t v = 0; v < morphedVertices.size(); v++) {
            KeyboardGLTF::Vec3 pos = morphedVertices[v].position;
            KeyboardGLTF::Vec2 uv = morphedVertices[v].texCoord;
            
            for (size_t w = 0; w < nodeState.weights.size() && w < surface.morphTargets.size(); w++) {
                float weight = nodeState.weights[w];
                const auto& target = surface.morphTargets[w];
                
                // Apply position morphs
                if (!target.position.empty() && v < target.position.size()) {
                    pos.x += target.position[v].x * weight;
                    pos.y += target.position[v].y * weight;
                    pos.z += target.position[v].z * weight;
                }
                
                // Apply UV morphs
                if (!target.texCoord.empty() && v < target.texCoord.size()) {
                    uv.x += target.texCoord[v].x * weight;
                    uv.y += target.texCoord[v].y * weight;
                }
            }
            
            morphedVertices[v].position = pos;
            morphedVertices[v].texCoord = uv;
        }
        
        // Update VBO with morphed vertices
        if (meshIdx < meshes_.size()) {
            auto& renderMesh = meshes_[meshIdx];
            if (renderMesh.vbo != 0) {
                glBindBuffer(GL_ARRAY_BUFFER, renderMesh.vbo);
                glBufferSubData(GL_ARRAY_BUFFER, 0, morphedVertices.size() * sizeof(KeyboardGLTF::Vertex), morphedVertices.data());
                glBindBuffer(GL_ARRAY_BUFFER, 0);
            }
        }
        
        dirtyMeshes_[meshIdx] = false;
    }
    
    glBindBuffer(GL_ARRAY_BUFFER, prevArrayBuffer);
}
