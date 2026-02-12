#include "vr_renderer.h"
#include "openxr_manager.h"
#include "openxr_swapchain.h"

#include <iostream>
#include <cstring>
#include <cmath>

#ifdef __ANDROID__
#define XR_USE_PLATFORM_ANDROID
#endif
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

using namespace std;

// Forward declarations from openxr_manager (declared in header with C++ linkage)
// These are already declared in openxr_manager.h, no need to redeclare

// VR renderer state
static struct {
    bool initialized;
    OpenXRSwapchain* leftSwapchain;
    OpenXRSwapchain* rightSwapchain;
    OpenXRSwapchain* quadSwapchain;  // HUD layer
    OpenXRSwapchain* djuiSwapchain;  // DJUI layer
    
    // OpenXR handles (from manager)
    XrInstance xrInstance;
    XrSession xrSession;
    XrSpace xrSpace;        // World/stage space
    XrSpace xrViewSpace;    // View space for head-locked layers
    XrSystemId xrSystemId;
    
    // Frame state
    XrFrameState frameState;
    bool frameActive;
    
    // View state
    XrView views[2];
    bool viewsValid;
    
    // Swapchain image indices
    uint32_t swapchainIndices[2];
    uint32_t quadSwapchainIndex;  // HUD layer
    uint32_t djuiSwapchainIndex;  // DJUI layer
} g_vr_renderer = {
    false,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    XR_NULL_HANDLE,
    XR_NULL_HANDLE,
    XR_NULL_HANDLE,
    XR_NULL_HANDLE,
    XR_NULL_SYSTEM_ID,
    {},
    false,
    {},
    false,
    {0, 0},
    0,
    0
};

int vr_renderer_init(void)
{
    if (g_vr_renderer.initialized) {
        std::cout << "VR renderer already initialized";
        return 1;
    }
    
    if (!openxr_is_initialized()) {
        std::cerr << "Cannot initialize VR renderer: OpenXR not initialized";
        return 0;
    }
    
    std::cout << "Initializing VR renderer...";
    
    // Get OpenXR handles from manager
    g_vr_renderer.xrInstance = openxr_get_instance();
    g_vr_renderer.xrSession = openxr_get_session();
    g_vr_renderer.xrSpace = openxr_get_space();
    g_vr_renderer.xrSystemId = openxr_get_system_id();
    
    if (g_vr_renderer.xrInstance == XR_NULL_HANDLE ||
        g_vr_renderer.xrSession == XR_NULL_HANDLE ||
        g_vr_renderer.xrSpace == XR_NULL_HANDLE ||
        g_vr_renderer.xrSystemId == XR_NULL_SYSTEM_ID) {
        std::cerr << "Failed to get OpenXR handles from manager";
        return 0;
    }
    
    // Create VIEW reference space for head-locked layers
    XrReferenceSpaceCreateInfo viewSpaceCreateInfo{};
    viewSpaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    viewSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    viewSpaceCreateInfo.poseInReferenceSpace = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    
    XrResult result = xrCreateReferenceSpace(g_vr_renderer.xrSession, &viewSpaceCreateInfo, &g_vr_renderer.xrViewSpace);
    if (result != XR_SUCCESS) {
        std::cerr << "Failed to create VIEW reference space: " << result;
        return 0;
    }
    std::cout << "Created VIEW reference space for head-locked layers";
    
    // Create swapchains
    if (!createOpenXRSwapchains(
            g_vr_renderer.xrInstance,
            g_vr_renderer.xrSystemId,
            g_vr_renderer.xrSession,
            &g_vr_renderer.leftSwapchain,
            &g_vr_renderer.rightSwapchain)) {
        std::cerr << "Failed to create OpenXR swapchains";
        return 0;
    }

    // use a resolution with the same aspect ratio as the left eye with some padding  2064x2208

    // Create quad swapchain for HUD layer (fixed size for now, e.g., 1280x720)
    if (!createQuadSwapchain(
            g_vr_renderer.xrInstance,
            g_vr_renderer.xrSystemId,
            g_vr_renderer.xrSession,
            1600, 1400,
            &g_vr_renderer.quadSwapchain)) {
        std::cerr << "Failed to create OpenXR HUD quad swapchain";
        return 0;
    }

    // Create DJUI swapchain (same size as HUD layer)
    if (!createQuadSwapchain(
            g_vr_renderer.xrInstance,
            g_vr_renderer.xrSystemId,
            g_vr_renderer.xrSession,
            1280, 720,
            &g_vr_renderer.djuiSwapchain)) {
        std::cerr << "Failed to create OpenXR DJUI quad swapchain";
        return 0;
    }

    
    std::cout << "VR renderer initialized successfully";
    std::cout << "Left eye: " << g_vr_renderer.leftSwapchain->width << "x" << g_vr_renderer.leftSwapchain->height;
    std::cout << "Right eye: " << g_vr_renderer.rightSwapchain->width << "x" << g_vr_renderer.rightSwapchain->height;
    
    g_vr_renderer.initialized = true;
    return 1;
}

void vr_renderer_shutdown(void)
{
    if (!g_vr_renderer.initialized) {
        return;
    }
    
    std::cout << "Shutting down VR renderer...";
    
    destroyOpenXRSwapchain(g_vr_renderer.leftSwapchain);
    destroyOpenXRSwapchain(g_vr_renderer.rightSwapchain);
    destroyOpenXRSwapchain(g_vr_renderer.quadSwapchain);
    destroyOpenXRSwapchain(g_vr_renderer.djuiSwapchain);
    
    g_vr_renderer.leftSwapchain = nullptr;
    g_vr_renderer.rightSwapchain = nullptr;
    g_vr_renderer.quadSwapchain = nullptr;
    g_vr_renderer.djuiSwapchain = nullptr;
    g_vr_renderer.initialized = false;
    
    std::cout << "VR renderer shutdown complete";
}

int vr_renderer_is_initialized(void)
{
    return g_vr_renderer.initialized ? 1 : 0;
}

int vr_renderer_begin_frame(void)
{
    if (!g_vr_renderer.initialized) {
        return 0;
    }
    
    // Frame state was already updated by openxr_update()
    // Just mark that we're starting a frame
    g_vr_renderer.frameActive = true;
    
    // Locate views to get eye poses
    XrViewState viewState{};
    viewState.type = XR_TYPE_VIEW_STATE;
    
    uint32_t viewCount = 2;
    g_vr_renderer.views[0].type = XR_TYPE_VIEW;
    g_vr_renderer.views[0].next = nullptr;
    g_vr_renderer.views[1].type = XR_TYPE_VIEW;
    g_vr_renderer.views[1].next = nullptr;
    
    XrViewLocateInfo viewLocateInfo{};
    viewLocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
    viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewLocateInfo.displayTime = g_vr_renderer.frameState.predictedDisplayTime;
    viewLocateInfo.space = g_vr_renderer.xrSpace;
    
    XrResult result = xrLocateViews(
        g_vr_renderer.xrSession,
        &viewLocateInfo,
        &viewState,
        viewCount,
        &viewCount,
        g_vr_renderer.views
    );
    
    if (result != XR_SUCCESS || viewCount != 2) {
        cerr << "Failed to locate views: " << result << endl;
        g_vr_renderer.viewsValid = false;
        return 0;
    }
    
    g_vr_renderer.viewsValid = true;
    return 1;
}

int vr_renderer_render_eye(int eye)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.frameActive || !g_vr_renderer.viewsValid) {
        return 0;
    }
    
    if (eye < 0 || eye > 1) {
        cerr << "Invalid eye index: " << eye << endl;
        return 0;
    }
    
    OpenXRSwapchain* swapchain = (eye == 0) ? g_vr_renderer.leftSwapchain : g_vr_renderer.rightSwapchain;
    
    // Acquire swapchain image
    XrSwapchainImageAcquireInfo acquireInfo{};
    acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    
    uint32_t imageIndex = 0;
    XrResult result = xrAcquireSwapchainImage(swapchain->swapchain, &acquireInfo, &imageIndex);
    
    if (result != XR_SUCCESS) {
        cerr << "Failed to acquire swapchain image for eye " << eye << ": " << result << endl;
        return 0;
    }
    
    g_vr_renderer.swapchainIndices[eye] = imageIndex;
    
    // Wait for swapchain image
    XrSwapchainImageWaitInfo waitInfo{};
    waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    waitInfo.timeout = XR_INFINITE_DURATION;
    
    result = xrWaitSwapchainImage(swapchain->swapchain, &waitInfo);
    
    if (result != XR_SUCCESS) {
        cerr << "Failed to wait for swapchain image for eye " << eye << ": " << result << endl;
        return 0;
    }
    
    // At this point, the game should render to swapchain->images[imageIndex]
    // This will be handled by the OpenGL integration
    
    return 1;
}

int vr_renderer_end_frame(void)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.frameActive) {
        return 0;
    }
    
    // Release swapchain images for both eyes
    for (int eye = 0; eye < 2; eye++) {
        OpenXRSwapchain* swapchain = (eye == 0) ? g_vr_renderer.leftSwapchain : g_vr_renderer.rightSwapchain;
        
        XrSwapchainImageReleaseInfo releaseInfo{};
        releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        
        XrResult result = xrReleaseSwapchainImage(swapchain->swapchain, &releaseInfo);
        
        if (result != XR_SUCCESS) {
            cerr << "Failed to release swapchain image for eye " << eye << ": " << result << endl;
        }
    }

    // Release quad swapchain images  
    vr_renderer_release_quad_images();
    
    // Submit frame to OpenXR with all layers (projection + quads)
    XrCompositionLayerProjectionView projectionViews[2]{};
    
    for (int eye = 0; eye < 2; eye++) {
        OpenXRSwapchain* swapchain = (eye == 0) ? g_vr_renderer.leftSwapchain : g_vr_renderer.rightSwapchain;
        
        projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        projectionViews[eye].pose = g_vr_renderer.views[eye].pose;
        projectionViews[eye].fov = g_vr_renderer.views[eye].fov;
        projectionViews[eye].subImage.swapchain = swapchain->swapchain;
        projectionViews[eye].subImage.imageRect.offset = {0, 0};
        projectionViews[eye].subImage.imageRect.extent = {(int32_t)swapchain->width, (int32_t)swapchain->height};
        projectionViews[eye].subImage.imageArrayIndex = 0;
    }
    
    XrCompositionLayerProjection projectionLayer{};
    projectionLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    projectionLayer.space = g_vr_renderer.xrSpace;
    projectionLayer.viewCount = 2;
    projectionLayer.views = projectionViews;
    
    
    // Create HUD quad layer - HEAD-LOCKED (uses VIEW space)
    XrCompositionLayerQuad hudLayer{};
    hudLayer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    hudLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // Enable alpha blending
    hudLayer.space = g_vr_renderer.xrViewSpace;  // Use VIEW space for head-locking
    hudLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    
    // Position HUD 1.0 meter forward in VIEW space (head-locked)
    // In VIEW space, -Z is forward, so we use negative Z
    float hud_distance = 1.0f;
    hudLayer.pose.position.x = 0.0f;
    hudLayer.pose.position.y = -0.2f;  // Move down 0.2 meters, feels more centered
    hudLayer.pose.position.z = -hud_distance;  // Forward is -Z in VIEW space
    
    // Identity orientation (no rotation needed in VIEW space)
    hudLayer.pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    
    // Size: 1.6m wide x 0.9m tall (16:9 aspect ratio) - doubled for better visibility
    hudLayer.size = {1.6f, 1.4f};
    
    hudLayer.subImage.swapchain = g_vr_renderer.quadSwapchain->swapchain;
    hudLayer.subImage.imageRect.offset = {0, 0};
    hudLayer.subImage.imageRect.extent = {(int32_t)g_vr_renderer.quadSwapchain->width, (int32_t)g_vr_renderer.quadSwapchain->height};
    hudLayer.subImage.imageArrayIndex = 0;

    
    // Create DJUI quad layer - HEAD-LOCKED (uses VIEW space)
    XrCompositionLayerQuad djuiLayer{};
    djuiLayer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    djuiLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // Enable alpha blending
    djuiLayer.space = g_vr_renderer.xrViewSpace;  // Use VIEW space for head-locking
    djuiLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    
    // Position DJUI 1.1 meters forward in VIEW space (head-locked)
    // In VIEW space, -Z is forward, so we use negative Z
    float djui_distance = 1.1f;
    djuiLayer.pose.position.x = 0.0f;
    djuiLayer.pose.position.y = -0.2f;
    djuiLayer.pose.position.z = -djui_distance;  // Forward is -Z in VIEW space
    
    // Identity orientation (no rotation needed in VIEW space)
    djuiLayer.pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    
    // Size: 1.0m wide x 0.5625m tall (16:9 aspect ratio)
    djuiLayer.size = {0.8f, 0.45f};
    
    djuiLayer.subImage.swapchain = g_vr_renderer.djuiSwapchain->swapchain;
    djuiLayer.subImage.imageRect.offset = {0, 0};
    djuiLayer.subImage.imageRect.extent = {(int32_t)g_vr_renderer.djuiSwapchain->width, (int32_t)g_vr_renderer.djuiSwapchain->height};
    djuiLayer.subImage.imageArrayIndex = 0;
    
    // Submit all layers (projection first, then quads on top)
    const XrCompositionLayerBaseHeader* layers[] = {
        (const XrCompositionLayerBaseHeader*)&projectionLayer,
        (const XrCompositionLayerBaseHeader*)&hudLayer,
        (const XrCompositionLayerBaseHeader*)&djuiLayer
    };
    
    XrFrameEndInfo frameEndInfo{};
    frameEndInfo.type = XR_TYPE_FRAME_END_INFO;
    frameEndInfo.displayTime = g_vr_renderer.frameState.predictedDisplayTime;
    frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    frameEndInfo.layerCount = 3;  // Projection + 2 quad layers
    frameEndInfo.layers = layers;
    
    XrResult result = xrEndFrame(g_vr_renderer.xrSession, &frameEndInfo);
    
    if (result != XR_SUCCESS) {
        cerr << "Failed to end OpenXR frame: " << result << endl;
        g_vr_renderer.frameActive = false;
        return 0;
    }
    
    g_vr_renderer.frameActive = false;
    return 1;
}

void vr_renderer_get_viewport(int eye, uint32_t* width, uint32_t* height)
{
    if (!g_vr_renderer.initialized || eye < 0 || eye > 1) {
        *width = 0;
        *height = 0;
        return;
    }
    
    OpenXRSwapchain* swapchain = (eye == 0) ? g_vr_renderer.leftSwapchain : g_vr_renderer.rightSwapchain;
    *width = swapchain->width;
    *height = swapchain->height;
}

// Helper function to convert XrFovf to projection matrix
static void fov_to_projection_matrix(const XrFovf& fov, float nearZ, float farZ, float* matrix)
{
    const float tanLeft = tanf(fov.angleLeft);
    const float tanRight = tanf(fov.angleRight);
    const float tanDown = tanf(fov.angleDown);
    const float tanUp = tanf(fov.angleUp);
    
    const float tanWidth = tanRight - tanLeft;
    const float tanHeight = tanUp - tanDown;
    
    // Standard OpenGL projection matrix
    memset(matrix, 0, 16 * sizeof(float));
    
    matrix[0] = 2.0f / tanWidth;
    matrix[4] = 0.0f;
    matrix[8] = (tanRight + tanLeft) / tanWidth;
    matrix[12] = 0.0f;
    
    matrix[1] = 0.0f;
    matrix[5] = 2.0f / tanHeight;
    matrix[9] = (tanUp + tanDown) / tanHeight;
    matrix[13] = 0.0f;
    
    matrix[2] = 0.0f;
    matrix[6] = 0.0f;
    matrix[10] = -(farZ + nearZ) / (farZ - nearZ);
    matrix[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    
    matrix[3] = 0.0f;
    matrix[7] = 0.0f;
    matrix[11] = -1.0f;
    matrix[15] = 0.0f;
}

int vr_renderer_get_projection_matrix_ext(int eye, float nearZ, float farZ, float* matrix)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.viewsValid || eye < 0 || eye > 1) {
        return 0;
    }
    
    // Convert OpenXR FOV to projection matrix
    fov_to_projection_matrix(g_vr_renderer.views[eye].fov, nearZ, farZ, matrix);
    
    return 1;
}

int vr_renderer_get_projection_matrix(int eye, float* matrix)
{
    // Use typical near/far plane values for SM64
    return vr_renderer_get_projection_matrix_ext(eye, 100.0f, 32000.0f, matrix);
}

// Helper function to convert XrPosef to view matrix, optionally removing yaw
static void pose_to_view_matrix(const XrPosef& pose, float* matrix, bool removeYaw)
{
    // Convert quaternion to rotation matrix
    XrQuaternionf q = pose.orientation;
    const XrVector3f& p = pose.position;
    
    XrVector3f pos = p;
    
    if (removeYaw) {
        // Extract yaw component from quaternion (rotation around Y axis)
        // A pure yaw rotation has form (0, sin(theta/2), 0, cos(theta/2))
        // So we extract y and w components and normalize
        float mag = sqrtf(q.y * q.y + q.w * q.w);
        
        if (mag > 0.001f) {
            // Create pure yaw quaternion
            XrQuaternionf yawQ = {0.0f, q.y / mag, 0.0f, q.w / mag};
            
            // Calculate inverse (conjugate) of yaw quaternion
            XrQuaternionf invYawQ = {-yawQ.x, -yawQ.y, -yawQ.z, yawQ.w};
            
            // Apply inverse yaw rotation to original orientation to remove yaw
            // newQ = invYawQ * q
            XrQuaternionf newQ;
            newQ.w = invYawQ.w * q.w - invYawQ.x * q.x - invYawQ.y * q.y - invYawQ.z * q.z;
            newQ.x = invYawQ.w * q.x + invYawQ.x * q.w + invYawQ.y * q.z - invYawQ.z * q.y;
            newQ.y = invYawQ.w * q.y - invYawQ.x * q.z + invYawQ.y * q.w + invYawQ.z * q.x;
            newQ.z = invYawQ.w * q.z + invYawQ.x * q.y - invYawQ.y * q.x + invYawQ.z * q.w;
            
            q = newQ;
            
            // Rotate position by negative yaw (invYawQ) to keep it relative to the now-yaw-aligned space
            // p_new = invYawQ * p * invYawQ_conj
            // Since invYawQ is around Y axis, we can use 2D rotation
            // invYawQ = (0, y, 0, w) where w=cos(-yaw/2), y=sin(-yaw/2)
            // standard rotation formula around Y axis:
            // x' = x*cos(theta) + z*sin(theta)
            // z' = -x*sin(theta) + z*cos(theta)
            
            // We can extract sin/cos directly from the half-angle quaternion components
            // sin(theta) = 2*w*y
            // cos(theta) = 1 - 2*y*y (or w*w - y*y)
            float sinTheta = 2.0f * invYawQ.w * invYawQ.y;
            float cosTheta = 1.0f - 2.0f * invYawQ.y * invYawQ.y;
            
            pos.x = p.x * cosTheta + p.z * sinTheta;
            pos.z = -p.x * sinTheta + p.z * cosTheta;
            // pos.y remains unchanged
        }
    }

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
    
    matrix[12] = -(rotMatrix[0] * pos.x + rotMatrix[1] * pos.y + rotMatrix[2] * pos.z);
    matrix[13] = -(rotMatrix[4] * pos.x + rotMatrix[5] * pos.y + rotMatrix[6] * pos.z);
    matrix[14] = -(rotMatrix[8] * pos.x + rotMatrix[9] * pos.y + rotMatrix[10] * pos.z);
    matrix[15] = 1.0f;
}

int vr_renderer_get_view_matrix(int eye, float* matrix)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.viewsValid || eye < 0 || eye > 1) {
        return 0;
    }
    
    pose_to_view_matrix(g_vr_renderer.views[eye].pose, matrix, false);
    
    return 1;
}

extern "C" int vr_renderer_get_view_matrix_no_yaw(int eye, float* matrix)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.viewsValid || eye < 0 || eye > 1) {
        return 0;
    }
    
    pose_to_view_matrix(g_vr_renderer.views[eye].pose, matrix, true);
    
    return 1;
}

// This function will be called by openxr_manager to update frame state
extern "C" void vr_renderer_set_frame_state(XrFrameState frameState)
{
    g_vr_renderer.frameState = frameState;
}

// Get the current swapchain GL texture for an eye
// This function needs C linkage so it can be called from vr_opengl.c
extern "C" GLuint vr_renderer_get_swapchain_texture(int eye)
{
    if (!g_vr_renderer.initialized || eye < 0 || eye > 1) {
        return 0;
    }
    
    OpenXRSwapchain* swapchain = (eye == 0) ? g_vr_renderer.leftSwapchain : g_vr_renderer.rightSwapchain;
    if (!swapchain || !swapchain->images) {
        return 0;
    }
    
    uint32_t imageIndex = g_vr_renderer.swapchainIndices[eye];
    if (imageIndex >= swapchain->imageCount) {
        return 0;
    }
    
    return swapchain->images[imageIndex];
}

// Get the swapchain texture format
GLenum vr_renderer_get_swapchain_format(int eye)
{
    if (!g_vr_renderer.initialized || eye < 0 || eye > 1) {
        return 0;
    }
    
    OpenXRSwapchain* swapchain = (eye == 0) ? g_vr_renderer.leftSwapchain : g_vr_renderer.rightSwapchain;
    if (!swapchain) {
        return 0;
    }
    
    return swapchain->format;
}

// Get the number of swapchain images per eye
uint32_t vr_renderer_get_swapchain_image_count(int eye)
{
    if (!g_vr_renderer.initialized || eye < 0 || eye > 1) {
        return 0;
    }
    
    OpenXRSwapchain* swapchain = (eye == 0) ? g_vr_renderer.leftSwapchain : g_vr_renderer.rightSwapchain;
    if (!swapchain) {
        return 0;
    }
    
    return swapchain->imageCount;
}

// Get the current quad layer swapchain GL texture
// This function needs C linkage so it can be called from vr_opengl.c
extern "C" GLuint vr_renderer_get_quad_swapchain_texture(void)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.quadSwapchain) {
        return 0;
    }
    
    if (g_vr_renderer.quadSwapchainIndex >= g_vr_renderer.quadSwapchain->imageCount) {
        return 0;
    }
    
    return g_vr_renderer.quadSwapchain->images[g_vr_renderer.quadSwapchainIndex];
}

// Get the quad layer dimensions
void vr_renderer_get_quad_dimensions(uint32_t* width, uint32_t* height)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.quadSwapchain) {
        *width = 0;
        *height = 0;
        return;
    }
    
    *width = g_vr_renderer.quadSwapchain->width;
    *height = g_vr_renderer.quadSwapchain->height;
}

// Get the current DJUI layer swapchain GL texture
// This function needs C linkage so it can be called from vr_opengl.c
extern "C" GLuint vr_renderer_get_djui_swapchain_texture(void)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.djuiSwapchain) {
        return 0;
    }
    
    if (g_vr_renderer.djuiSwapchainIndex >= g_vr_renderer.djuiSwapchain->imageCount) {
        return 0;
    }
    
    return g_vr_renderer.djuiSwapchain->images[g_vr_renderer.djuiSwapchainIndex];
}

// Get the DJUI layer dimensions
void vr_renderer_get_djui_dimensions(uint32_t* width, uint32_t* height)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.djuiSwapchain) {
        *width = 0;
        *height = 0;
        return;
    }
    
    *width = g_vr_renderer.djuiSwapchain->width;
    *height = g_vr_renderer.djuiSwapchain->height;
}

// Acquire and wait for quad swapchain images
extern "C" int vr_renderer_acquire_quad_images(void)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.frameActive) {
        return 0;
    }
    
    // Acquire HUD quad swapchain image
    XrSwapchainImageAcquireInfo acquireInfo{};
    acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    
    uint32_t quadImageIndex = 0;
    XrResult result = xrAcquireSwapchainImage(g_vr_renderer.quadSwapchain->swapchain, &acquireInfo, &quadImageIndex);
    if (result != XR_SUCCESS) {
        cerr << "Failed to acquire HUD quad swapchain image: " << result << endl;
        return 0;
    }
    
    g_vr_renderer.quadSwapchainIndex = quadImageIndex;
    
    // Wait for HUD quad swapchain image
    XrSwapchainImageWaitInfo waitInfo{};
    waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    waitInfo.timeout = XR_INFINITE_DURATION;
    
    result = xrWaitSwapchainImage(g_vr_renderer.quadSwapchain->swapchain, &waitInfo);
    if (result != XR_SUCCESS) {
        cerr << "Failed to wait for HUD quad swapchain image: " << result << endl;
        return 0;
    }
    
    // Acquire DJUI quad swapchain image
    uint32_t djuiImageIndex = 0;
    result = xrAcquireSwapchainImage(g_vr_renderer.djuiSwapchain->swapchain, &acquireInfo, &djuiImageIndex);
    if (result != XR_SUCCESS) {
        cerr << "Failed to acquire DJUI quad swapchain image: " << result << endl;
        return 0;
    }
    
    g_vr_renderer.djuiSwapchainIndex = djuiImageIndex;
    
    // Wait for DJUI quad swapchain image
    result = xrWaitSwapchainImage(g_vr_renderer.djuiSwapchain->swapchain, &waitInfo);
    if (result != XR_SUCCESS) {
        cerr << "Failed to wait for DJUI quad swapchain image: " << result << endl;
        return 0;
    }
    
    return 1;
}

// Release quad swapchain images
extern "C" void vr_renderer_release_quad_images(void)
{
    if (!g_vr_renderer.initialized) {
        return;
    }
    
    XrSwapchainImageReleaseInfo releaseInfo{};
    releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    
    // Release HUD quad swapchain
    if (g_vr_renderer.quadSwapchain) {
        XrResult result = xrReleaseSwapchainImage(g_vr_renderer.quadSwapchain->swapchain, &releaseInfo);
        if (result != XR_SUCCESS) {
            cerr << "Failed to release HUD quad swapchain image: " << result << endl;
        }
    }
    
    // Release DJUI quad swapchain
    if (g_vr_renderer.djuiSwapchain) {
        XrResult result = xrReleaseSwapchainImage(g_vr_renderer.djuiSwapchain->swapchain, &releaseInfo);
        if (result != XR_SUCCESS) {
            cerr << "Failed to release DJUI quad swapchain image: " << result << endl;
        }
    }
}

extern "C" void vr_renderer_update_space(void)
{
    if (!g_vr_renderer.initialized) {
        return;
    }
    
    g_vr_renderer.xrSpace = openxr_get_space();
}

