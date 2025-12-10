#ifndef OPENXR_KEYBOARD_H
#define OPENXR_KEYBOARD_H

#include <openxr/openxr.h>

// C interface for calling from C code
#ifdef __cplusplus
#include <vector>
#include <string>
#include <map>
#include <set>
extern "C" {
#endif

void openxr_show_keyboard(void);
void openxr_hide_keyboard(void);
int openxr_is_keyboard_visible(void);
void openxr_render_keyboard(int eye);

#ifdef __cplusplus
}
#endif

// C++ interface
#ifdef __cplusplus

#ifdef USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/gl.h>
#endif

#include "openxr_keyboard_gltf.h"

struct VirtualKeyboardLocation {
    XrPosef pose;
    float scale;
};

// Mesh data for rendering
struct KeyboardMesh {
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLuint vao = 0;
    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;
    uint64_t textureId = 0;
    float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float transform[16] = { // Identity matrix by default
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    int nodeIndex = -1; // Index of the node this mesh belongs to
};

// Texture data
struct KeyboardTexture {
    GLuint textureId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Node state for runtime animation and transforms (matches Meta's ModelNodeState)
// Separates dynamic state from static model data
struct KeyboardNodeState {
    // Pointer to static node data in the model
    const KeyboardGLTF::Node* node = nullptr;
    
    // Runtime transform components (modified by animations)
    float translation[3] = {0.0f, 0.0f, 0.0f};
    float scale[3] = {1.0f, 1.0f, 1.0f};
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // Quaternion (x, y, z, w)
    
    // Runtime weights (modified by animations)
    std::vector<float> weights;
    
    // Cached transforms (recomputed when transform components change)
    float localTransform[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    float globalTransform[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    
    // Initial values for animation reset
    float initialTranslation[3] = {0.0f, 0.0f, 0.0f};
    float initialScale[3] = {1.0f, 1.0f, 1.0f};
    float initialRotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    
    // Methods matching Meta's API
    void CalculateLocalTransform();
    const float* GetLocalTransform() const { return localTransform; }
    const float* GetGlobalTransform() const { return globalTransform; }
};

// Model state - contains all runtime state for the keyboard model
struct KeyboardModelState {
    std::vector<KeyboardNodeState> nodeStates;
    std::map<int, int> nodeToFirstMesh;  // Maps node index → first mesh index (for legacy VBO lookup)
    
    void GenerateFromModel(const KeyboardGLTF::Model& model);
    void RecalculateGlobalTransforms();
};

class OpenXRKeyboard {
public:
    static OpenXRKeyboard& GetInstance();

    bool Init(XrInstance instance, XrSession session);
    void Shutdown();
    void Update(XrSpace currentSpace, XrTime predictedDisplayTime, const XrPosef& headPose);
    
    bool CreateKeyboard();
    bool DestroyKeyboard();
    
    bool ShowKeyboard();
    bool HideKeyboard();
    bool IsVisible() const { return isVisible_; }
    void Render(int eye);

    bool SuggestLocation(const XrVirtualKeyboardLocationInfoMETA* locationInfo);
    bool GetLocation(XrSpace baseSpace, XrTime time, VirtualKeyboardLocation* keyboardLocation);
    
    // Rendering support
    bool GetModelAnimationStates(XrVirtualKeyboardModelAnimationStatesMETA& modelAnimationStates);
    bool GetDirtyTextures(std::vector<uint64_t>& textureIds);
    bool GetTextureData(uint64_t textureId, XrVirtualKeyboardTextureDataMETA& textureData);
    
    // Input support
    bool SendInput(XrSpace space, XrVirtualKeyboardInputSourceMETA source, const XrPosef& pointerPose, bool pressed, XrPosef* interactorRootPose);
    bool UpdateTextContext(const std::string& textContext);
    
    // Model loading
    bool LoadKeyboardModel();
    void UnloadKeyboardModel();

private:
    OpenXRKeyboard() = default;
    
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrVirtualKeyboardMETA keyboardHandle_ = XR_NULL_HANDLE;
    XrSpace space_ = XR_NULL_HANDLE;
    
    // Frame state
    XrSpace currentSpace_ = XR_NULL_HANDLE;
    XrTime currentDisplayTime_ = 0;
    XrPosef currentHeadPose_ = {{0,0,0,1}, {0,0,0}};
    
    bool isVisible_ = false;
    bool initialized_ = false;
    bool recenterRequested_ = false;
    
    // Function pointers
    PFN_xrCreateVirtualKeyboardMETA xrCreateVirtualKeyboardMETA_ = nullptr;
    PFN_xrDestroyVirtualKeyboardMETA xrDestroyVirtualKeyboardMETA_ = nullptr;
    PFN_xrCreateVirtualKeyboardSpaceMETA xrCreateVirtualKeyboardSpaceMETA_ = nullptr;
    PFN_xrSuggestVirtualKeyboardLocationMETA xrSuggestVirtualKeyboardLocationMETA_ = nullptr;
    PFN_xrGetVirtualKeyboardScaleMETA xrGetVirtualKeyboardScaleMETA_ = nullptr;
    PFN_xrSetVirtualKeyboardModelVisibilityMETA xrSetVirtualKeyboardModelVisibilityMETA_ = nullptr;
    PFN_xrGetVirtualKeyboardModelAnimationStatesMETA xrGetVirtualKeyboardModelAnimationStatesMETA_ = nullptr;
    PFN_xrGetVirtualKeyboardDirtyTexturesMETA xrGetVirtualKeyboardDirtyTexturesMETA_ = nullptr;
    PFN_xrGetVirtualKeyboardTextureDataMETA xrGetVirtualKeyboardTextureDataMETA_ = nullptr;
    PFN_xrSendVirtualKeyboardInputMETA xrSendVirtualKeyboardInputMETA_ = nullptr;
    PFN_xrChangeVirtualKeyboardTextContextMETA xrChangeVirtualKeyboardTextContextMETA_ = nullptr;
    
    // Render model extension function pointers
    PFN_xrEnumerateRenderModelPathsFB xrEnumerateRenderModelPathsFB_ = nullptr;
    PFN_xrGetRenderModelPropertiesFB xrGetRenderModelPropertiesFB_ = nullptr;
    PFN_xrLoadRenderModelFB xrLoadRenderModelFB_ = nullptr;

    std::vector<XrVirtualKeyboardAnimationStateMETA> animationStatesBuffer_;
    std::vector<uint8_t> textureDataBuffer_;
    std::vector<uint8_t> modelDataBuffer_;
    std::vector<KeyboardMesh> meshes_;
    std::map<uint64_t, KeyboardTexture> textures_;

    // Rendering data
    bool modelLoaded_ = false;
    bool texturesLoaded_ = false;
    bool isModelAnimated_ = false;
    XrRenderModelKeyFB modelKey_ = XR_NULL_RENDER_MODEL_KEY_FB;
    GLuint shaderProgram_ = 0;
    
    // Helper methods
    bool InitRenderModelExtension();
    bool CreateShaderProgram();
    bool ParseGLTFModel(const uint8_t* data, size_t size);
    void UpdateTextures();
    void CleanupGL();
    
    // Animation support
    KeyboardGLTF::Model gltfModel_;  // Static model data (never modified after load)
    KeyboardModelState modelState_;   // Runtime state (weights, transforms, dirty flags)
    void UpdateAnimations(const XrVirtualKeyboardModelAnimationStatesMETA& animationStates);
    void ApplyMorphTargets();
    
    std::set<int> activeAnimatedNodes_;
    
    // Dirty node tracking for batched geometry updates
    std::vector<bool> dirtyMeshes_;  // Track which meshes need geometry updates
    void MarkMeshDirty(size_t meshIndex);
    void ApplyDirtyMeshUpdates();  // Apply all pending geometry updates at once

    // Ray rendering
    struct RayState {
        bool active = false;
        XrPosef pose = {{0,0,0,1}, {0,0,0}};
        XrPosef interactorRootPose = {{0,0,0,1}, {0,0,0}};
    };
    
    RayState leftRay_;
    RayState rightRay_;
    
    GLuint rayShaderProgram_ = 0;
    GLuint rayVao_ = 0;
    GLuint rayVbo_ = 0;
    
    bool CreateRayResources();
    void RenderRays(const float* viewMatrix, const float* projMatrix);
};

#endif // __cplusplus

#endif // OPENXR_KEYBOARD_H
