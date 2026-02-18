#ifndef OPENXR_SWAPCHAIN_H
#define OPENXR_SWAPCHAIN_H

#include "openxr_platform_defines.h"

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Swapchain wrapper for a single eye
typedef struct {
    XrSwapchain swapchain;
    GLenum format;
    uint32_t width;
    uint32_t height;
    uint32_t imageCount;
    GLuint* images;  // OpenGL texture IDs
} OpenXRSwapchain;

// Create swapchains for both eyes
// Returns 1 on success, 0 on failure
// Left and right swapchain pointers will be allocated and must be freed with destroyOpenXRSwapchain
int createOpenXRSwapchains(
    XrInstance instance,
    XrSystemId systemId,
    XrSession session,
    OpenXRSwapchain** leftSwapchain,
    OpenXRSwapchain** rightSwapchain
);

// Create a swapchain for a quad layer
// Returns 1 on success, 0 on failure
int createQuadSwapchain(
    XrInstance instance,
    XrSystemId systemId,
    XrSession session,
    uint32_t width,
    uint32_t height,
    OpenXRSwapchain** swapchain
);

// Destroy swapchain and free memory
void destroyOpenXRSwapchain(OpenXRSwapchain* swapchain);

// Get view configuration for stereo rendering
int getViewConfiguration(
    XrInstance instance,
    XrSystemId systemId,
    uint32_t* viewCount,
    XrViewConfigurationView* views
);

#ifdef __cplusplus
}
#endif

#endif // OPENXR_SWAPCHAIN_H

