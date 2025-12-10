#include "vr_renderer.h"
#include "openxr_manager.h"
#include "openxr_swapchain.h"

#include <iostream>
#include <cstring>
#include <cmath>

#define XR_USE_GRAPHICS_API_VULKAN
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
    XrSpace xrSpace;
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
        cout << "VR renderer already initialized" << endl;
        return 1;
    }
    
    if (!openxr_is_initialized()) {
        cerr << "Cannot initialize VR renderer: OpenXR not initialized" << endl;
        return 0;
    }
    
    cout << "Initializing VR renderer..." << endl;
    
    // Get OpenXR handles from manager
    g_vr_renderer.xrInstance = openxr_get_instance();
    g_vr_renderer.xrSession = openxr_get_session();
    g_vr_renderer.xrSpace = openxr_get_space();
    g_vr_renderer.xrSystemId = openxr_get_system_id();
    
    if (g_vr_renderer.xrInstance == XR_NULL_HANDLE ||
        g_vr_renderer.xrSession == XR_NULL_HANDLE ||
        g_vr_renderer.xrSpace == XR_NULL_HANDLE ||
        g_vr_renderer.xrSystemId == XR_NULL_SYSTEM_ID) {
        cerr << "Failed to get OpenXR handles from manager" << endl;
        return 0;
    }
    
    // Create swapchains
    if (!createOpenXRSwapchains(
            g_vr_renderer.xrInstance,
            g_vr_renderer.xrSystemId,
            g_vr_renderer.xrSession,
            &g_vr_renderer.leftSwapchain,
            &g_vr_renderer.rightSwapchain)) {
        cerr << "Failed to create OpenXR swapchains" << endl;
        return 0;
    }

    // Create quad swapchain for HUD layer (fixed size for now, e.g., 1280x720)
    if (!createQuadSwapchain(
            g_vr_renderer.xrInstance,
            g_vr_renderer.xrSystemId,
            g_vr_renderer.xrSession,
            1280, 720,
            &g_vr_renderer.quadSwapchain)) {
        cerr << "Failed to create OpenXR HUD quad swapchain" << endl;
        return 0;
    }

    // Create DJUI swapchain (same size as HUD layer)
    if (!createQuadSwapchain(
            g_vr_renderer.xrInstance,
            g_vr_renderer.xrSystemId,
            g_vr_renderer.xrSession,
            1280, 720,
            &g_vr_renderer.djuiSwapchain)) {
        cerr << "Failed to create OpenXR DJUI quad swapchain" << endl;
        return 0;
    }

    
    cout << "VR renderer initialized successfully" << endl;
    cout << "Left eye: " << g_vr_renderer.leftSwapchain->width 
         << "x" << g_vr_renderer.leftSwapchain->height << endl;
    cout << "Right eye: " << g_vr_renderer.rightSwapchain->width 
         << "x" << g_vr_renderer.rightSwapchain->height << endl;
    
    g_vr_renderer.initialized = true;
    return 1;
}

void vr_renderer_shutdown(void)
{
    if (!g_vr_renderer.initialized) {
        return;
    }
    
    cout << "Shutting down VR renderer..." << endl;
    
    destroyOpenXRSwapchain(g_vr_renderer.leftSwapchain);
    destroyOpenXRSwapchain(g_vr_renderer.rightSwapchain);
    destroyOpenXRSwapchain(g_vr_renderer.quadSwapchain);
    destroyOpenXRSwapchain(g_vr_renderer.djuiSwapchain);
    
    g_vr_renderer.leftSwapchain = nullptr;
    g_vr_renderer.rightSwapchain = nullptr;
    g_vr_renderer.quadSwapchain = nullptr;
    g_vr_renderer.djuiSwapchain = nullptr;
    g_vr_renderer.initialized = false;
    
    cout << "VR renderer shutdown complete" << endl;
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
    
    // Release swapchain images
    for (int eye = 0; eye < 2; eye++) {
        OpenXRSwapchain* swapchain = (eye == 0) ? g_vr_renderer.leftSwapchain : g_vr_renderer.rightSwapchain;
        
        XrSwapchainImageReleaseInfo releaseInfo{};
        releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        
        XrResult result = xrReleaseSwapchainImage(swapchain->swapchain, &releaseInfo);
        
        if (result != XR_SUCCESS) {
            cerr << "Failed to release swapchain image for eye " << eye << ": " << result << endl;
        }
    }

    // Handle HUD Quad Layer
    if (g_vr_renderer.quadSwapchain) {
        XrSwapchainImageAcquireInfo acquireInfo{};
        acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
        uint32_t imageIndex;
        if (xrAcquireSwapchainImage(g_vr_renderer.quadSwapchain->swapchain, &acquireInfo, &imageIndex) == XR_SUCCESS) {
            XrSwapchainImageWaitInfo waitInfo{};
            waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
            waitInfo.timeout = XR_INFINITE_DURATION;
            if (xrWaitSwapchainImage(g_vr_renderer.quadSwapchain->swapchain, &waitInfo) == XR_SUCCESS) {
                
                // Store the HUD quad swapchain index for use by the copy system
                g_vr_renderer.quadSwapchainIndex = imageIndex;
                
                // Note: The actual rendering to the HUD quad layer will be done by vr_opengl
                // and copied to the swapchain image by vr_copy

                XrSwapchainImageReleaseInfo releaseInfo{};
                releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
                xrReleaseSwapchainImage(g_vr_renderer.quadSwapchain->swapchain, &releaseInfo);
            }
        }
    }

    // Handle DJUI Quad Layer
    if (g_vr_renderer.djuiSwapchain) {
        XrSwapchainImageAcquireInfo acquireInfo{};
        acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
        uint32_t imageIndex;
        if (xrAcquireSwapchainImage(g_vr_renderer.djuiSwapchain->swapchain, &acquireInfo, &imageIndex) == XR_SUCCESS) {
            XrSwapchainImageWaitInfo waitInfo{};
            waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
            waitInfo.timeout = XR_INFINITE_DURATION;
            if (xrWaitSwapchainImage(g_vr_renderer.djuiSwapchain->swapchain, &waitInfo) == XR_SUCCESS) {
                
                // Store the DJUI quad swapchain index for use by the copy system
                g_vr_renderer.djuiSwapchainIndex = imageIndex;
                
                // Note: The actual rendering to the DJUI quad layer will be done by vr_opengl
                // and copied to the swapchain image by vr_copy

                XrSwapchainImageReleaseInfo releaseInfo{};
                releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
                xrReleaseSwapchainImage(g_vr_renderer.djuiSwapchain->swapchain, &releaseInfo);
            }
        }
    }
    
    // Submit frame to OpenXR
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
    
    XrCompositionLayerProjection layer{};
    layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    layer.space = g_vr_renderer.xrSpace;
    layer.viewCount = 2;
    layer.views = projectionViews;
    


    // Quad Layer - Head-locked (follows player rotation)
    // Calculate center position between both eyes
    XrPosef centerPose{};
    centerPose.position.x = (g_vr_renderer.views[0].pose.position.x + g_vr_renderer.views[1].pose.position.x) / 2.0f;
    centerPose.position.y = (g_vr_renderer.views[0].pose.position.y + g_vr_renderer.views[1].pose.position.y) / 2.0f;
    centerPose.position.z = (g_vr_renderer.views[0].pose.position.z + g_vr_renderer.views[1].pose.position.z) / 2.0f;
    
    // Use the orientation from the left eye (both should be very similar)
    centerPose.orientation = g_vr_renderer.views[0].pose.orientation;
    
    // Calculate forward vector from the head orientation
    XrQuaternionf q = centerPose.orientation;
    XrVector3f forward;
    // Negate X and Y to get the -Z (Forward) direction
    forward.x = -2.0f * (q.x * q.z + q.w * q.y);
    forward.y = -2.0f * (q.y * q.z - q.w * q.x);
    forward.z = -1.0f + 2.0f * (q.x * q.x + q.y * q.y);
    
    // Position the quad 1.5 meters in front of the head
    float distance = 1.5f;
    XrVector3f quadPosition;
    quadPosition.x = centerPose.position.x + forward.x * distance;
    quadPosition.y = centerPose.position.y + forward.y * distance - 0.3f; 
    quadPosition.z = centerPose.position.z + forward.z * distance;
    
    // Use the head orientation directly so the quad rotates with the head
    XrQuaternionf quadOrientation = centerPose.orientation;
    
    // HUD Quad Layer - Head-locked
    XrCompositionLayerQuad hudQuadLayer{};
    hudQuadLayer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    hudQuadLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT; // Enable alpha blending
    hudQuadLayer.space = g_vr_renderer.xrSpace;
    hudQuadLayer.subImage.swapchain = g_vr_renderer.quadSwapchain->swapchain;
    hudQuadLayer.subImage.imageRect.offset = {0, 0};
    hudQuadLayer.subImage.imageRect.extent = {(int32_t)g_vr_renderer.quadSwapchain->width, (int32_t)g_vr_renderer.quadSwapchain->height};
    hudQuadLayer.subImage.imageArrayIndex = 0;
    hudQuadLayer.pose.orientation = quadOrientation;
    hudQuadLayer.pose.position = quadPosition; // Position in front of head
    hudQuadLayer.size = {2.0f, 2.0f}; // 2x2 meter
    hudQuadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;

    // DJUI Quad Layer - Head-locked (same position as HUD, but rendered on top)
    XrCompositionLayerQuad djuiQuadLayer{};
    djuiQuadLayer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    djuiQuadLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT; // Enable alpha blending
    djuiQuadLayer.space = g_vr_renderer.xrSpace;
    djuiQuadLayer.subImage.swapchain = g_vr_renderer.djuiSwapchain->swapchain;
    djuiQuadLayer.subImage.imageRect.offset = {0, 0};
    djuiQuadLayer.subImage.imageRect.extent = {(int32_t)g_vr_renderer.djuiSwapchain->width, (int32_t)g_vr_renderer.djuiSwapchain->height};
    djuiQuadLayer.subImage.imageArrayIndex = 0;
    djuiQuadLayer.pose.orientation = quadOrientation;
    djuiQuadLayer.pose.position = quadPosition; // Same position as HUD
    djuiQuadLayer.size = {1.0f, 1.0f}; // 1x1 meter
    djuiQuadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;

    // Submit layers in order: projection (3D world), HUD, DJUI
    const XrCompositionLayerBaseHeader* layers[] = {
        (const XrCompositionLayerBaseHeader*)&layer,
        (const XrCompositionLayerBaseHeader*)&hudQuadLayer,
        (const XrCompositionLayerBaseHeader*)&djuiQuadLayer
    };
    
    XrFrameEndInfo frameEndInfo{};
    frameEndInfo.type = XR_TYPE_FRAME_END_INFO;
    frameEndInfo.displayTime = g_vr_renderer.frameState.predictedDisplayTime;
    frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    frameEndInfo.layerCount = 3;  // projection + HUD + DJUI
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

// Helper function to convert XrPosef to view matrix
static void pose_to_view_matrix(const XrPosef& pose, float* matrix)
{
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

int vr_renderer_get_view_matrix(int eye, float* matrix)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.viewsValid || eye < 0 || eye > 1) {
        return 0;
    }
    
    pose_to_view_matrix(g_vr_renderer.views[eye].pose, matrix);
    
    return 1;
}

// This function will be called by openxr_manager to update frame state
extern "C" void vr_renderer_set_frame_state(XrFrameState frameState)
{
    g_vr_renderer.frameState = frameState;
}

// Get the current swapchain image for an eye
VkImage vr_renderer_get_swapchain_image(int eye)
{
    if (!g_vr_renderer.initialized || eye < 0 || eye > 1) {
        return VK_NULL_HANDLE;
    }
    
    OpenXRSwapchain* swapchain = (eye == 0) ? g_vr_renderer.leftSwapchain : g_vr_renderer.rightSwapchain;
    if (!swapchain || !swapchain->images) {
        return VK_NULL_HANDLE;
    }
    
    uint32_t imageIndex = g_vr_renderer.swapchainIndices[eye];
    if (imageIndex >= swapchain->imageCount) {
        return VK_NULL_HANDLE;
    }
    
    return swapchain->images[imageIndex];
}

// Get the swapchain image format
uint32_t vr_renderer_get_swapchain_format(int eye)
{
    if (!g_vr_renderer.initialized || eye < 0 || eye > 1) {
        return 0;
    }
    
    OpenXRSwapchain* swapchain = (eye == 0) ? g_vr_renderer.leftSwapchain : g_vr_renderer.rightSwapchain;
    if (!swapchain) {
        return 0;
    }
    
    return (uint32_t)swapchain->format;
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

// Get the current quad layer swapchain image
VkImage vr_renderer_get_quad_swapchain_image(void)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.quadSwapchain) {
        return VK_NULL_HANDLE;
    }
    
    if (g_vr_renderer.quadSwapchainIndex >= g_vr_renderer.quadSwapchain->imageCount) {
        return VK_NULL_HANDLE;
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

// Get the current DJUI layer swapchain image
VkImage vr_renderer_get_djui_swapchain_image(void)
{
    if (!g_vr_renderer.initialized || !g_vr_renderer.djuiSwapchain) {
        return VK_NULL_HANDLE;
    }
    
    if (g_vr_renderer.djuiSwapchainIndex >= g_vr_renderer.djuiSwapchain->imageCount) {
        return VK_NULL_HANDLE;
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
