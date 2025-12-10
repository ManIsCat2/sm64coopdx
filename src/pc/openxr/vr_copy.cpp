#include "vr_copy.h"
#include "vr_renderer.h"
#include "vr_opengl.h"
#include "openxr_manager.h"

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <iostream>

#ifdef __MINGW32__
# define FOR_WINDOWS 1
#else
# define FOR_WINDOWS 0
#endif

#if FOR_WINDOWS || defined(OSX_BUILD)
# define GLEW_STATIC
# include <GL/glew.h>
#endif

#define GL_GLEXT_PROTOTYPES 1

#ifdef WAPI_SDL2
# include <SDL2/SDL.h>
# ifdef USE_GLES
#  include <SDL2/SDL_opengles2.h>
# else
#  include <SDL2/SDL_opengl.h>
# endif
#elif defined(WAPI_SDL1)
# include <SDL/SDL.h>
# ifndef GLEW_STATIC
#  include <SDL/SDL_opengl.h>
# endif
#endif

#ifdef __linux__
#include <unistd.h>
#endif


#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#ifndef APIENTRY
#define APIENTRY
#endif

#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

using namespace std;

// OpenGL extension constants
#ifndef GL_TEXTURE_TILING_EXT
#define GL_TEXTURE_TILING_EXT             0x9580
#define GL_TILING_TYPES_EXT               0x9583
#define GL_OPTIMAL_TILING_EXT             0x9584
#define GL_LINEAR_TILING_EXT              0x9585
#define GL_HANDLE_TYPE_OPAQUE_FD_EXT      0x9586
#define GL_HANDLE_TYPE_OPAQUE_WIN32_EXT   0x9587
#endif

// Extension function pointers (for functions not in core)
typedef void (APIENTRYP PFNGLIMPORTMEMORYFDEXTPROC_LOCAL) (GLuint memory, GLuint64 size, GLenum handleType, GLint fd);
typedef void (APIENTRYP PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC_LOCAL) (GLuint memory, GLuint64 size, GLenum handleType, void *handle);

#ifdef __linux__
static PFNGLIMPORTMEMORYFDEXTPROC_LOCAL glImportMemoryFdEXT_ptr = nullptr;
#elif FOR_WINDOWS
static PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC_LOCAL glImportMemoryWin32HandleEXT_ptr = nullptr;
#endif

// Vulkan extension function pointers (for extensions not in core)
#ifdef __linux__
static PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR_ptr = nullptr;
#endif
#if FOR_WINDOWS
static PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR_ptr = nullptr;
#endif

// VR copy state for each eye
struct VRCopyEyeState {
    // OpenGL objects
    GLuint glMemoryObject;
    GLuint glTexture;
    GLuint glFramebuffer;
    
    // Vulkan objects (for intermediate images if needed)
    VkImage vkIntermediateImage;
    VkDeviceMemory vkImageMemory;
    int memoryFd;  // For Linux
    void* memoryHandle;  // For Windows
    
    // Vulkan staging buffer for pixel transfer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    void* stagingMapped;  // Persistently mapped staging memory
    uint32_t width;
    uint32_t height;
    
    bool initialized;
};

static struct {
    bool initialized;
    bool extensionsLoaded;
    bool useIntermediateImages;  // True if we need to create intermediate Vulkan images
    
    VRCopyEyeState eyes[2];
    VRCopyEyeState quadState;  // For HUD quad layer
    VRCopyEyeState djuiState;  // For DJUI quad layer
    
    VkDevice vkDevice;
    VkPhysicalDevice vkPhysicalDevice;
    VkQueue vkQueue;
    int32_t queueFamilyIndex;
    
    // Vulkan command buffer for copy operations
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
} g_vr_copy = {
    false,
    false,
    false,
    {},
    {},
    {},  // quadState
    VK_NULL_HANDLE,
    VK_NULL_HANDLE,
    VK_NULL_HANDLE,
    -1,
    VK_NULL_HANDLE,
    VK_NULL_HANDLE
};

// Load OpenGL extensions
static bool load_gl_extensions(void)
{
    if (g_vr_copy.extensionsLoaded) {
        return true;
    }
    
    printf("Loading OpenGL memory object extensions...\n");
    
    // Check for required extension
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    if (!extensions) {
        fprintf(stderr, "Failed to get OpenGL extensions\n");
        return false;
    }
    
    if (!strstr(extensions, "GL_EXT_memory_object")) {
        fprintf(stderr, "GL_EXT_memory_object not supported\n");
        return false;
    }
    
#ifdef __linux__
    if (!strstr(extensions, "GL_EXT_memory_object_fd")) {
        fprintf(stderr, "GL_EXT_memory_object_fd not supported\n");
        return false;
    }
#elif FOR_WINDOWS
    if (!strstr(extensions, "GL_EXT_memory_object_win32")) {
        fprintf(stderr, "GL_EXT_memory_object_win32 not supported\n");
        return false;
    }
#endif
    
    // Load extension function pointers (if needed)
#ifdef __linux__
    glImportMemoryFdEXT_ptr = (PFNGLIMPORTMEMORYFDEXTPROC_LOCAL)SDL_GL_GetProcAddress("glImportMemoryFdEXT");
    if (!glImportMemoryFdEXT_ptr) {
        fprintf(stderr, "Failed to load glImportMemoryFdEXT\n");
        return false;
    }
#elif FOR_WINDOWS
    glImportMemoryWin32HandleEXT_ptr = (PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC_LOCAL)SDL_GL_GetProcAddress("glImportMemoryWin32HandleEXT");
    if (!glImportMemoryWin32HandleEXT_ptr) {
        fprintf(stderr, "Failed to load glImportMemoryWin32HandleEXT\n");
        return false;
    }
#endif
    
    printf("OpenGL memory object extensions loaded successfully\n");
    g_vr_copy.extensionsLoaded = true;
    return true;
}

// Load Vulkan function pointers
static bool load_vk_extensions(void)
{
    VkInstance vkInstance = openxr_get_vulkan_instance();
    if (vkInstance == VK_NULL_HANDLE) {
        fprintf(stderr, "Failed to get Vulkan instance\n");
        return false;
    }
    
    printf("Loading Vulkan extension functions...\n");
    
#ifdef __linux__
    vkGetMemoryFdKHR_ptr = (PFN_vkGetMemoryFdKHR)vkGetInstanceProcAddr(vkInstance, "vkGetMemoryFdKHR");
    if (!vkGetMemoryFdKHR_ptr) {
        fprintf(stderr, "Failed to load vkGetMemoryFdKHR\n");
        return false;
    }
#elif FOR_WINDOWS
    vkGetMemoryWin32HandleKHR_ptr = (PFN_vkGetMemoryWin32HandleKHR)vkGetInstanceProcAddr(vkInstance, "vkGetMemoryWin32HandleKHR");
    if (!vkGetMemoryWin32HandleKHR_ptr) {
        fprintf(stderr, "Failed to load vkGetMemoryWin32HandleKHR\n");
        return false;
    }
#endif
    
    printf("Vulkan extension functions loaded successfully\n");
    return true;
}

// Find Vulkan memory type with required properties
static uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(g_vr_copy.vkPhysicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    fprintf(stderr, "Failed to find suitable memory type\n");
    return 0;
}

// Convert Vulkan format to OpenGL internal format
static GLenum vk_format_to_gl_format(uint32_t vkFormat)
{
    switch (vkFormat) {
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM:
            return GL_RGBA;
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
            return GL_RGBA;
        case VK_FORMAT_R8G8B8_SRGB:
        case VK_FORMAT_R8G8B8_UNORM:
            return GL_RGB;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return GL_RGBA;
        default:
            fprintf(stderr, "Warning: Unsupported Vulkan format %u, using GL_RGBA\n", vkFormat);
            return GL_RGBA;
    }
}

// Initialize interop for a single eye
static bool init_eye_interop(int eye)
{
    printf("Initializing VR copy interop for eye %d...\n", eye);
    
    VRCopyEyeState* eyeState = &g_vr_copy.eyes[eye];
    if (eyeState->initialized) {
        printf("Eye %d already initialized\n", eye);
        return true;
    }
    
    // Get viewport dimensions
    uint32_t width, height;
    vr_renderer_get_viewport(eye, &width, &height);
    
    if (width == 0 || height == 0) {
        fprintf(stderr, "Invalid viewport dimensions for eye %d\n", eye);
        return false;
    }
    
    printf("Eye %d viewport: %ux%u\n", eye, width, height);
    
    // Get swapchain image and format
    VkImage swapchainImage = vr_renderer_get_swapchain_image(eye);
    uint32_t vkFormat = vr_renderer_get_swapchain_format(eye);
    
    if (swapchainImage == VK_NULL_HANDLE || vkFormat == 0) {
        fprintf(stderr, "Failed to get swapchain image or format for eye %d\n", eye);
        return false;
    }
    
    printf("Eye %d: VkImage=%p, format=%u\n", eye, (void*)swapchainImage, vkFormat);
    
    // For now, we'll use a simpler approach: just use glReadPixels and Vulkan staging
    // This is slower but guaranteed to work. We can optimize later.
    printf("Using simple pixel readback approach for eye %d\n", eye);
    
    // Create OpenGL texture for blitting
    glGenTextures(1, &eyeState->glTexture);
    glBindTexture(GL_TEXTURE_2D, eyeState->glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // Create framebuffer for the texture
    glGenFramebuffers(1, &eyeState->glFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, eyeState->glFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, eyeState->glTexture, 0);
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer incomplete for eye %d: 0x%x\n", eye, status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Store dimensions
    eyeState->width = width;
    eyeState->height = height;
    
    // Create Vulkan staging buffer for pixel transfer
    VkDeviceSize bufferSize = width * height * 4;  // RGBA8
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VkResult result = vkCreateBuffer(g_vr_copy.vkDevice, &bufferInfo, nullptr, &eyeState->stagingBuffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create staging buffer for eye %d: %d\n", eye, result);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_vr_copy.vkDevice, eyeState->stagingBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = find_memory_type(memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    result = vkAllocateMemory(g_vr_copy.vkDevice, &allocInfo, nullptr, &eyeState->stagingMemory);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate staging memory for eye %d: %d\n", eye, result);
        vkDestroyBuffer(g_vr_copy.vkDevice, eyeState->stagingBuffer, nullptr);
        eyeState->stagingBuffer = VK_NULL_HANDLE;
        return false;
    }
    
    result = vkBindBufferMemory(g_vr_copy.vkDevice, eyeState->stagingBuffer, eyeState->stagingMemory, 0);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to bind staging buffer memory for eye %d: %d\n", eye, result);
        vkFreeMemory(g_vr_copy.vkDevice, eyeState->stagingMemory, nullptr);
        vkDestroyBuffer(g_vr_copy.vkDevice, eyeState->stagingBuffer, nullptr);
        eyeState->stagingBuffer = VK_NULL_HANDLE;
        eyeState->stagingMemory = VK_NULL_HANDLE;
        return false;
    }
    
    // Persistently map the staging memory
    result = vkMapMemory(g_vr_copy.vkDevice, eyeState->stagingMemory, 0, bufferSize, 0, &eyeState->stagingMapped);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to map staging memory for eye %d: %d\n", eye, result);
        vkFreeMemory(g_vr_copy.vkDevice, eyeState->stagingMemory, nullptr);
        vkDestroyBuffer(g_vr_copy.vkDevice, eyeState->stagingBuffer, nullptr);
        eyeState->stagingBuffer = VK_NULL_HANDLE;
        eyeState->stagingMemory = VK_NULL_HANDLE;
        return false;
    }
    
    printf("Created staging buffer for eye %d: %lu bytes\n", eye, (unsigned long)bufferSize);
    
    eyeState->initialized = true;
    printf("VR copy interop initialized for eye %d\n", eye);
    
    return true;
}

int vr_copy_init(void)
{
    if (g_vr_copy.initialized) {
        printf("VR copy already initialized\n");
        return 1;
    }
    
    printf("Initializing VR copy system...\n");
    
    // Check prerequisites
    if (!vr_renderer_is_initialized()) {
        fprintf(stderr, "VR renderer not initialized\n");
        return 0;
    }
    
    if (!vr_opengl_is_initialized()) {
        fprintf(stderr, "VR OpenGL not initialized\n");
        return 0;
    }
    
    // Get Vulkan handles
    g_vr_copy.vkDevice = openxr_get_vulkan_device();
    g_vr_copy.vkPhysicalDevice = openxr_get_vulkan_physical_device();
    g_vr_copy.vkQueue = openxr_get_vulkan_queue();
    g_vr_copy.queueFamilyIndex = openxr_get_vulkan_queue_family_index();
    
    if (g_vr_copy.vkDevice == VK_NULL_HANDLE || g_vr_copy.vkPhysicalDevice == VK_NULL_HANDLE) {
        fprintf(stderr, "Failed to get Vulkan handles\n");
        return 0;
    }
    
    // Load extensions
    if (!load_gl_extensions()) {
        fprintf(stderr, "Failed to load OpenGL extensions, will use fallback method\n");
        // Continue anyway with simple readback
    }
    
    if (!load_vk_extensions()) {
        fprintf(stderr, "Failed to load Vulkan extensions, will use fallback method\n");
        // Continue anyway with simple readback
    }
    
    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = g_vr_copy.queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    VkResult result = vkCreateCommandPool(g_vr_copy.vkDevice, &poolInfo, nullptr, &g_vr_copy.commandPool);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create command pool: %d\n", result);
        return 0;
    }
    
    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = g_vr_copy.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    result = vkAllocateCommandBuffers(g_vr_copy.vkDevice, &allocInfo, &g_vr_copy.commandBuffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate command buffer: %d\n", result);
        vkDestroyCommandPool(g_vr_copy.vkDevice, g_vr_copy.commandPool, nullptr);
        g_vr_copy.commandPool = VK_NULL_HANDLE;
        return 0;
    }
    
    // Initialize interop for both eyes
    for (int eye = 0; eye < 2; eye++) {
        if (!init_eye_interop(eye)) {
            fprintf(stderr, "Failed to initialize interop for eye %d\n", eye);
            vr_copy_shutdown();
            return 0;
        }
    }
    
    // Initialize interop for quad layer
    printf("Initializing quad layer interop...\n");
    
    // Get quad layer dimensions
    uint32_t quadWidth, quadHeight;
    vr_renderer_get_quad_dimensions(&quadWidth, &quadHeight);
    
    if (quadWidth == 0 || quadHeight == 0) {
        fprintf(stderr, "Invalid quad layer dimensions\n");
        vr_copy_shutdown();
        return 0;
    }
    
    // Initialize quad state similar to eyes
    VRCopyEyeState* quadState = &g_vr_copy.quadState;
    
    // Create OpenGL texture for blitting
    glGenTextures(1, &quadState->glTexture);
    glBindTexture(GL_TEXTURE_2D, quadState->glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, quadWidth, quadHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // Create framebuffer for the texture
    glGenFramebuffers(1, &quadState->glFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, quadState->glFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, quadState->glTexture, 0);
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer incomplete for quad layer: 0x%x\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        vr_copy_shutdown();
        return 0;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Store dimensions
    quadState->width = quadWidth;
    quadState->height = quadHeight;
    
    // Create Vulkan staging buffer
    VkDeviceSize bufferSize = quadWidth * quadHeight * 4;  // RGBA8
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VkResult quadResult = vkCreateBuffer(g_vr_copy.vkDevice, &bufferInfo, nullptr, &quadState->stagingBuffer);
    if (quadResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to create staging buffer for quad layer: %d\n", quadResult);
        vr_copy_shutdown();
        return 0;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_vr_copy.vkDevice, quadState->stagingBuffer, &memRequirements);
    
    VkMemoryAllocateInfo quadMemAllocInfo{};
    quadMemAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    quadMemAllocInfo.allocationSize = memRequirements.size;
    quadMemAllocInfo.memoryTypeIndex = find_memory_type(memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    quadResult = vkAllocateMemory(g_vr_copy.vkDevice, &quadMemAllocInfo, nullptr, &quadState->stagingMemory);
    if (quadResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate staging memory for quad layer: %d\n", quadResult);
        vkDestroyBuffer(g_vr_copy.vkDevice, quadState->stagingBuffer, nullptr);
        quadState->stagingBuffer = VK_NULL_HANDLE;
        vr_copy_shutdown();
        return 0;
    }
    
    quadResult = vkBindBufferMemory(g_vr_copy.vkDevice, quadState->stagingBuffer, quadState->stagingMemory, 0);
    if (quadResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to bind staging buffer memory for quad layer: %d\n", quadResult);
        vkFreeMemory(g_vr_copy.vkDevice, quadState->stagingMemory, nullptr);
        vkDestroyBuffer(g_vr_copy.vkDevice, quadState->stagingBuffer, nullptr);
        quadState->stagingBuffer = VK_NULL_HANDLE;
        quadState->stagingMemory = VK_NULL_HANDLE;
        vr_copy_shutdown();
        return 0;
    }
    
    // Persistently map the staging memory
    quadResult = vkMapMemory(g_vr_copy.vkDevice, quadState->stagingMemory, 0, bufferSize, 0, &quadState->stagingMapped);
    if (quadResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to map staging memory for quad layer: %d\n", quadResult);
        vkFreeMemory(g_vr_copy.vkDevice, quadState->stagingMemory, nullptr);
        vkDestroyBuffer(g_vr_copy.vkDevice, quadState->stagingBuffer, nullptr);
        quadState->stagingBuffer = VK_NULL_HANDLE;
        quadState->stagingMemory = VK_NULL_HANDLE;
        vr_copy_shutdown();
        return 0;
    }
    
    quadState->initialized = true;
    printf("Quad layer interop initialized: %ux%u\n", quadWidth, quadHeight);
    
    // Initialize interop for DJUI layer
    printf("Initializing DJUI layer interop...\n");
    
    // Get DJUI layer dimensions
    uint32_t djuiWidth, djuiHeight;
    vr_renderer_get_djui_dimensions(&djuiWidth, &djuiHeight);
    
    if (djuiWidth == 0 || djuiHeight == 0) {
        fprintf(stderr, "Invalid DJUI layer dimensions\n");
        vr_copy_shutdown();
        return 0;
    }
    
    // Initialize DJUI state similar to quad
    VRCopyEyeState* djuiState = &g_vr_copy.djuiState;
    
    // Create OpenGL texture for blitting
    glGenTextures(1, &djuiState->glTexture);
    glBindTexture(GL_TEXTURE_2D, djuiState->glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, djuiWidth, djuiHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // Create framebuffer for the texture
    glGenFramebuffers(1, &djuiState->glFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, djuiState->glFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, djuiState->glTexture, 0);
    
    GLenum djuiStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (djuiStatus != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer incomplete for DJUI layer: 0x%x\n", djuiStatus);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        vr_copy_shutdown();
        return 0;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Store dimensions
    djuiState->width = djuiWidth;
    djuiState->height = djuiHeight;
    
    // Create Vulkan staging buffer
    VkDeviceSize djuiBufferSize = djuiWidth * djuiHeight * 4;  // RGBA8
    
    VkBufferCreateInfo djuiBufferInfo{};
    djuiBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    djuiBufferInfo.size = djuiBufferSize;
    djuiBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    djuiBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VkResult djuiResult = vkCreateBuffer(g_vr_copy.vkDevice, &djuiBufferInfo, nullptr, &djuiState->stagingBuffer);
    if (djuiResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to create staging buffer for DJUI layer: %d\n", djuiResult);
        vr_copy_shutdown();
        return 0;
    }
    
    VkMemoryRequirements djuiMemRequirements;
    vkGetBufferMemoryRequirements(g_vr_copy.vkDevice, djuiState->stagingBuffer, &djuiMemRequirements);
    
    VkMemoryAllocateInfo djuiMemAllocInfo{};
    djuiMemAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    djuiMemAllocInfo.allocationSize = djuiMemRequirements.size;
    djuiMemAllocInfo.memoryTypeIndex = find_memory_type(djuiMemRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    djuiResult = vkAllocateMemory(g_vr_copy.vkDevice, &djuiMemAllocInfo, nullptr, &djuiState->stagingMemory);
    if (djuiResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate staging memory for DJUI layer: %d\n", djuiResult);
        vkDestroyBuffer(g_vr_copy.vkDevice, djuiState->stagingBuffer, nullptr);
        djuiState->stagingBuffer = VK_NULL_HANDLE;
        vr_copy_shutdown();
        return 0;
    }
    
    djuiResult = vkBindBufferMemory(g_vr_copy.vkDevice, djuiState->stagingBuffer, djuiState->stagingMemory, 0);
    if (djuiResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to bind staging buffer memory for DJUI layer: %d\n", djuiResult);
        vkFreeMemory(g_vr_copy.vkDevice, djuiState->stagingMemory, nullptr);
        vkDestroyBuffer(g_vr_copy.vkDevice, djuiState->stagingBuffer, nullptr);
        djuiState->stagingBuffer = VK_NULL_HANDLE;
        djuiState->stagingMemory = VK_NULL_HANDLE;
        vr_copy_shutdown();
        return 0;
    }
    
    // Persistently map the staging memory
    djuiResult = vkMapMemory(g_vr_copy.vkDevice, djuiState->stagingMemory, 0, djuiBufferSize, 0, &djuiState->stagingMapped);
    if (djuiResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to map staging memory for DJUI layer: %d\n", djuiResult);
        vkFreeMemory(g_vr_copy.vkDevice, djuiState->stagingMemory, nullptr);
        vkDestroyBuffer(g_vr_copy.vkDevice, djuiState->stagingBuffer, nullptr);
        djuiState->stagingBuffer = VK_NULL_HANDLE;
        djuiState->stagingMemory = VK_NULL_HANDLE;
        vr_copy_shutdown();
        return 0;
    }
    
    djuiState->initialized = true;
    printf("DJUI layer interop initialized: %ux%u\n", djuiWidth, djuiHeight);
    
    g_vr_copy.initialized = true;
    printf("VR copy system initialized successfully\n");
    
    return 1;
}

void vr_copy_shutdown(void)
{
    if (!g_vr_copy.initialized) {
        return;
    }
    
    printf("Shutting down VR copy system...\n");
    
    // Clean up each eye
    for (int eye = 0; eye < 2; eye++) {
        VRCopyEyeState* eyeState = &g_vr_copy.eyes[eye];
        
        if (eyeState->stagingMapped && eyeState->stagingMemory != VK_NULL_HANDLE) {
            vkUnmapMemory(g_vr_copy.vkDevice, eyeState->stagingMemory);
            eyeState->stagingMapped = nullptr;
        }
        
        if (eyeState->stagingBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(g_vr_copy.vkDevice, eyeState->stagingBuffer, nullptr);
            eyeState->stagingBuffer = VK_NULL_HANDLE;
        }
        
        if (eyeState->stagingMemory != VK_NULL_HANDLE) {
            vkFreeMemory(g_vr_copy.vkDevice, eyeState->stagingMemory, nullptr);
            eyeState->stagingMemory = VK_NULL_HANDLE;
        }
        
        if (eyeState->glFramebuffer != 0) {
            glDeleteFramebuffers(1, &eyeState->glFramebuffer);
            eyeState->glFramebuffer = 0;
        }
        
        if (eyeState->glTexture != 0) {
            glDeleteTextures(1, &eyeState->glTexture);
            eyeState->glTexture = 0;
        }
        
        if (eyeState->glMemoryObject != 0) {
            glDeleteMemoryObjectsEXT(1, &eyeState->glMemoryObject);
            eyeState->glMemoryObject = 0;
        }
        
        if (eyeState->vkImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(g_vr_copy.vkDevice, eyeState->vkImageMemory, nullptr);
            eyeState->vkImageMemory = VK_NULL_HANDLE;
        }
        
        eyeState->initialized = false;
    }
    
    // Clean up quad layer
    VRCopyEyeState* quadState = &g_vr_copy.quadState;
    
    if (quadState->stagingMapped && quadState->stagingMemory != VK_NULL_HANDLE) {
        vkUnmapMemory(g_vr_copy.vkDevice, quadState->stagingMemory);
        quadState->stagingMapped = nullptr;
    }
    
    if (quadState->stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(g_vr_copy.vkDevice, quadState->stagingBuffer, nullptr);
        quadState->stagingBuffer = VK_NULL_HANDLE;
    }
    
    if (quadState->stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(g_vr_copy.vkDevice, quadState->stagingMemory, nullptr);
        quadState->stagingMemory = VK_NULL_HANDLE;
    }
    
    if (quadState->glFramebuffer != 0) {
        glDeleteFramebuffers(1, &quadState->glFramebuffer);
        quadState->glFramebuffer = 0;
    }
    
    if (quadState->glTexture != 0) {
        glDeleteTextures(1, &quadState->glTexture);
        quadState->glTexture = 0;
    }
    
    if (quadState->glMemoryObject != 0) {
        glDeleteMemoryObjectsEXT(1, &quadState->glMemoryObject);
        quadState->glMemoryObject = 0;
    }
    
    if (quadState->vkImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(g_vr_copy.vkDevice, quadState->vkImageMemory, nullptr);
        quadState->vkImageMemory = VK_NULL_HANDLE;
    }
    
    quadState->initialized = false;
    
    // Clean up DJUI layer
    VRCopyEyeState* djuiState = &g_vr_copy.djuiState;
    
    if (djuiState->stagingMapped && djuiState->stagingMemory != VK_NULL_HANDLE) {
        vkUnmapMemory(g_vr_copy.vkDevice, djuiState->stagingMemory);
        djuiState->stagingMapped = nullptr;
    }
    
    if (djuiState->stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(g_vr_copy.vkDevice, djuiState->stagingBuffer, nullptr);
        djuiState->stagingBuffer = VK_NULL_HANDLE;
    }
    
    if (djuiState->stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(g_vr_copy.vkDevice, djuiState->stagingMemory, nullptr);
        djuiState->stagingMemory = VK_NULL_HANDLE;
    }
    
    if (djuiState->glFramebuffer != 0) {
        glDeleteFramebuffers(1, &djuiState->glFramebuffer);
        djuiState->glFramebuffer = 0;
    }
    
    if (djuiState->glTexture != 0) {
        glDeleteTextures(1, &djuiState->glTexture);
        djuiState->glTexture = 0;
    }
    
    if (djuiState->glMemoryObject != 0) {
        glDeleteMemoryObjectsEXT(1, &djuiState->glMemoryObject);
        djuiState->glMemoryObject = 0;
    }
    
    if (djuiState->vkImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(g_vr_copy.vkDevice, djuiState->vkImageMemory, nullptr);
        djuiState->vkImageMemory = VK_NULL_HANDLE;
    }
    
    djuiState->initialized = false;
    
    // Clean up command pool (this also frees command buffers)
    if (g_vr_copy.commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(g_vr_copy.vkDevice, g_vr_copy.commandPool, nullptr);
        g_vr_copy.commandPool = VK_NULL_HANDLE;
        g_vr_copy.commandBuffer = VK_NULL_HANDLE;
    }
    
    g_vr_copy.initialized = false;
    printf("VR copy system shutdown complete\n");
}

int vr_copy_is_initialized(void)
{
    return g_vr_copy.initialized ? 1 : 0;
}

// Helper: flip an RGBA8 image in-place (height rows, width pixels per row)
static void flip_y_rgba8(uint8_t* data, uint32_t width, uint32_t height) {
    if (!data || width == 0 || height == 0) return;
    const uint32_t row_bytes = width * 4; // RGBA8
    uint8_t* tmp = (uint8_t*)malloc(row_bytes);
    if (!tmp) return;

    for (uint32_t y = 0; y < height / 2; ++y) {
        uint8_t* row_top = data + y * row_bytes;
        uint8_t* row_bot = data + (height - 1 - y) * row_bytes;
        memcpy(tmp, row_top, row_bytes);
        memcpy(row_top, row_bot, row_bytes);
        memcpy(row_bot, tmp, row_bytes);
    }
    free(tmp);
}

int vr_copy_framebuffer_to_swapchain(int eye)
{
    static int frame_count = 0;
    static int logged_once = 0;

    if (!g_vr_copy.initialized || eye < 0 || eye > 1) {
        if (!logged_once) {
            fprintf(stderr, "vr_copy not initialized or invalid eye index\n");
            logged_once = 1;
        }
        return 0;
    }

    VRCopyEyeState* eyeState = &g_vr_copy.eyes[eye];
    if (!eyeState->initialized || !eyeState->stagingMapped) {
        if (!logged_once) {
            fprintf(stderr, "Eye %d state not initialized or staging not mapped\n", eye);
            logged_once = 1;
        }
        return 0;
    }

    // 1) Get the source framebuffer (from VR OpenGL)
    GLuint sourceFBO = vr_opengl_get_framebuffer(eye);
    if (sourceFBO == 0) {
        fprintf(stderr, "Failed to get source framebuffer for eye %d\n", eye);
        return 0;
    }

    const uint32_t width  = eyeState->width;
    const uint32_t height = eyeState->height;

    if (frame_count < 5) {
        printf("DEBUG vr_copy: Frame %d, Eye %d, FBO=%u, %ux%u\n",
               frame_count, eye, (unsigned)sourceFBO, width, height);
    }
    if (eye == 1) {
        if (frame_count < 100) frame_count++;
    }

    // 2) Save current FBO and pixel-pack alignment
    GLint oldFB = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFB);

    GLint oldPack = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &oldPack);
    // glPixelStorei(GL_PACK_ALIGNMENT, 1); // tightly packed
    // apparently faster in adreno
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    // 3) Bind the source FBO and read pixels (ES2 path — no blit, no separate read/draw targets)
    glBindFramebuffer(GL_FRAMEBUFFER, sourceFBO);

    // Read bottom-left-origin pixels into staging buffer…
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, eyeState->stagingMapped);

    // 4) Flip in CPU to convert from GL's bottom-left to Vulkan's top-left
    flip_y_rgba8((uint8_t*)eyeState->stagingMapped, width, height);

    // 5) Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, oldFB);
    glPixelStorei(GL_PACK_ALIGNMENT, oldPack);

    // Ensure GL writes are visible before Vulkan reads
    glFinish();

    // 6) Vulkan copy
    VkImage swapchainImage = vr_renderer_get_swapchain_image(eye);
    if (swapchainImage == VK_NULL_HANDLE) {
        fprintf(stderr, "Failed to get swapchain image for eye %d\n", eye);
        return 0;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult vkResult = vkBeginCommandBuffer(g_vr_copy.commandBuffer, &beginInfo);
    if (vkResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to begin command buffer for eye %d: %d\n", eye, vkResult);
        return 0;
    }

    // Transition swapchain image to TRANSFER_DST_OPTIMAL
    // Use UNDEFINED as old layout since OpenXR swapchain images come from a pool
    // and we can't reliably know their previous layout. UNDEFINED tells Vulkan
    // we don't care about previous contents (which is correct for a fresh frame).
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchainImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        g_vr_copy.commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;     // tightly packed
    region.bufferImageHeight = 0;   // tightly packed
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = (VkOffset3D){0, 0, 0};
    region.imageExtent = (VkExtent3D){width, height, 1};

    vkCmdCopyBufferToImage(
        g_vr_copy.commandBuffer,
        eyeState->stagingBuffer,
        swapchainImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    // Transition to COLOR_ATTACHMENT_OPTIMAL for OpenXR rendering
    // Note: OpenXR expects COLOR_ATTACHMENT_OPTIMAL for presentation
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    vkCmdPipelineBarrier(
        g_vr_copy.commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vkEndCommandBuffer(g_vr_copy.commandBuffer);

    // Submit with proper synchronization
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_vr_copy.commandBuffer;

    VkResult result = vkQueueSubmit(g_vr_copy.vkQueue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to submit Vulkan copy command for eye %d: %d\n", eye, result);
        return 0;
    }
    
    // Wait for the copy to complete before returning
    // This ensures the image is fully written before OpenXR releases it
    vkQueueWaitIdle(g_vr_copy.vkQueue);
    vkResetCommandBuffer(g_vr_copy.commandBuffer, 0);

    if (frame_count < 5) {
        printf("DEBUG vr_copy: Copy completed successfully for eye %d\n", eye);
    }
    return 1;
}

// Copy quad layer framebuffer to Vulkan swapchain
int vr_copy_quad_to_swapchain(void)
{
    static int logged_once = 0;

    if (!g_vr_copy.initialized) {
        if (!logged_once) {
            fprintf(stderr, "vr_copy not initialized\n");
            logged_once = 1;
        }
        return 0;
    }

    VRCopyEyeState* quadState = &g_vr_copy.quadState;
    if (!quadState->initialized || !quadState->stagingMapped) {
        if (!logged_once) {
            fprintf(stderr, "Quad state not initialized or staging not mapped\n");
            logged_once = 1;
        }
        return 0;
    }

    // Get the source framebuffer (from VR OpenGL)
    GLuint sourceFBO = vr_opengl_get_quad_framebuffer();
    if (sourceFBO == 0) {
        fprintf(stderr, "Failed to get quad framebuffer\n");
        return 0;
    }

    const uint32_t width  = quadState->width;
    const uint32_t height = quadState->height;

    // Save current FBO and pixel-pack alignment
    GLint oldFB = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFB);

    GLint oldPack = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &oldPack);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    // Bind the source FBO and read pixels
    glBindFramebuffer(GL_FRAMEBUFFER, sourceFBO);

    // Read bottom-left-origin pixels into staging buffer
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, quadState->stagingMapped);

    // Flip in CPU to convert from GL's bottom-left to Vulkan's top-left
    flip_y_rgba8((uint8_t*)quadState->stagingMapped, width, height);

    // Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, oldFB);
    glPixelStorei(GL_PACK_ALIGNMENT, oldPack);

    // Ensure GL writes are visible before Vulkan reads
    glFinish();

    // Vulkan copy
    VkImage swapchainImage = vr_renderer_get_quad_swapchain_image();
    if (swapchainImage == VK_NULL_HANDLE) {
        fprintf(stderr, "Failed to get quad swapchain image\n");
        return 0;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult vkResult = vkBeginCommandBuffer(g_vr_copy.commandBuffer, &beginInfo);
    if (vkResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to begin command buffer for quad: %d\n", vkResult);
        return 0;
    }

    // Transition swapchain image to TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchainImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        g_vr_copy.commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;     // tightly packed
    region.bufferImageHeight = 0;   // tightly packed
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = (VkOffset3D){0, 0, 0};
    region.imageExtent = (VkExtent3D){width, height, 1};

    vkCmdCopyBufferToImage(
        g_vr_copy.commandBuffer,
        quadState->stagingBuffer,
        swapchainImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    // Transition to COLOR_ATTACHMENT_OPTIMAL for OpenXR rendering
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    vkCmdPipelineBarrier(
        g_vr_copy.commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vkEndCommandBuffer(g_vr_copy.commandBuffer);

    // Submit with proper synchronization
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_vr_copy.commandBuffer;

    VkResult result = vkQueueSubmit(g_vr_copy.vkQueue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to submit Vulkan copy command for quad: %d\n", result);
        return 0;
    }
    
    // Wait for the copy to complete before returning
    vkQueueWaitIdle(g_vr_copy.vkQueue);
    vkResetCommandBuffer(g_vr_copy.commandBuffer, 0);

    return 1;
}

// Copy DJUI quad layer framebuffer to Vulkan swapchain
int vr_copy_djui_to_swapchain(void)
{
    static int logged_once = 0;

    if (!g_vr_copy.initialized) {
        if (!logged_once) {
            fprintf(stderr, "vr_copy not initialized\n");
            logged_once = 1;
        }
        return 0;
    }

    VRCopyEyeState* djuiState = &g_vr_copy.djuiState;
    if (!djuiState->initialized || !djuiState->stagingMapped) {
        if (!logged_once) {
            fprintf(stderr, "DJUI state not initialized or staging not mapped\n");
            logged_once = 1;
        }
        return 0;
    }

    // Get the source framebuffer (from VR OpenGL)
    GLuint sourceFBO = vr_opengl_get_djui_framebuffer();
    if (sourceFBO == 0) {
        fprintf(stderr, "Failed to get DJUI framebuffer\n");
        return 0;
    }

    const uint32_t width  = djuiState->width;
    const uint32_t height = djuiState->height;

    // Save current FBO and pixel-pack alignment
    GLint oldFB = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFB);

    GLint oldPack = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &oldPack);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    // Bind the source FBO and read pixels
    glBindFramebuffer(GL_FRAMEBUFFER, sourceFBO);

    // Read bottom-left-origin pixels into staging buffer
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, djuiState->stagingMapped);

    // Flip in CPU to convert from GL's bottom-left to Vulkan's top-left
    flip_y_rgba8((uint8_t*)djuiState->stagingMapped, width, height);

    // Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, oldFB);
    glPixelStorei(GL_PACK_ALIGNMENT, oldPack);

    // Ensure GL writes are visible before Vulkan reads
    glFinish();

    // Vulkan copy
    VkImage swapchainImage = vr_renderer_get_djui_swapchain_image();
    if (swapchainImage == VK_NULL_HANDLE) {
        fprintf(stderr, "Failed to get DJUI swapchain image\n");
        return 0;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult vkResult = vkBeginCommandBuffer(g_vr_copy.commandBuffer, &beginInfo);
    if (vkResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to begin command buffer for DJUI: %d\n", vkResult);
        return 0;
    }

    // Transition swapchain image to TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchainImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        g_vr_copy.commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;     // tightly packed
    region.bufferImageHeight = 0;   // tightly packed
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = (VkOffset3D){0, 0, 0};
    region.imageExtent = (VkExtent3D){width, height, 1};

    vkCmdCopyBufferToImage(
        g_vr_copy.commandBuffer,
        djuiState->stagingBuffer,
        swapchainImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    // Transition to COLOR_ATTACHMENT_OPTIMAL for OpenXR rendering
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    vkCmdPipelineBarrier(
        g_vr_copy.commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vkEndCommandBuffer(g_vr_copy.commandBuffer);

    // Submit with proper synchronization
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_vr_copy.commandBuffer;

    VkResult result = vkQueueSubmit(g_vr_copy.vkQueue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to submit Vulkan copy command for DJUI: %d\n", result);
        return 0;
    }
    
    // Wait for the copy to complete before returning
    vkQueueWaitIdle(g_vr_copy.vkQueue);
    vkResetCommandBuffer(g_vr_copy.commandBuffer, 0);

    return 1;
}
