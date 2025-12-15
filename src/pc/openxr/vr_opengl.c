#include "vr_opengl.h"
#include "vr_renderer.h"
#include "openxr_keyboard.h"
#include "vr_copy.h"
#include "pc/gfx/gfx_pc.h"
#include "game/game_init.h"

#ifdef RAPI_GL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// VR OpenGL state
static struct {
    int initialized;
    
    // Framebuffers for each eye
    GLuint framebuffers[2];
    
    // Color textures for each eye
    GLuint colorTextures[2];
    
    // Depth renderbuffers for each eye
    GLuint depthRenderbuffers[2];
    
    // Viewport dimensions for each eye
    uint32_t width[2];
    uint32_t height[2];
    
    // HUD quad layer (for game HUD overlay)
    GLuint quadFramebuffer;
    GLuint quadColorTexture;
    uint32_t quadWidth;
    uint32_t quadHeight;
    
    // DJUI quad layer (for UI overlay)
    GLuint djuiFramebuffer;
    GLuint djuiColorTexture;
    uint32_t djuiWidth;
    uint32_t djuiHeight;
    
    // Currently active eye (-1 if none)
    int activeEye;
    
    // Previous framebuffer binding (to restore after rendering)
    GLint previousFramebuffer;
} g_vr_opengl = {
    0,
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    -1,
    0
};

int vr_opengl_init(void)
{
    if (g_vr_opengl.initialized) {
        printf("VR OpenGL already initialized\n");
        return 1;
    }
    
    if (!vr_renderer_is_initialized()) {
        fprintf(stderr, "Cannot initialize VR OpenGL: VR renderer not initialized\n");
        return 0;
    }
    
    printf("Initializing VR OpenGL integration...\n");
    
    // Get viewport dimensions from VR renderer
    for (int eye = 0; eye < 2; eye++) {
        vr_renderer_get_viewport(eye, &g_vr_opengl.width[eye], &g_vr_opengl.height[eye]);
        
        printf("Eye %d viewport: %ux%u\n", eye, g_vr_opengl.width[eye], g_vr_opengl.height[eye]);
        
        if (g_vr_opengl.width[eye] == 0 || g_vr_opengl.height[eye] == 0) {
            fprintf(stderr, "Invalid viewport dimensions for eye %d\n", eye);
            return 0;
        }
    }
    
    // Generate framebuffers
    glGenFramebuffers(2, g_vr_opengl.framebuffers);
    
    // Generate textures
    glGenTextures(2, g_vr_opengl.colorTextures);
    
    // Generate depth renderbuffers
    glGenRenderbuffers(2, g_vr_opengl.depthRenderbuffers);
    
    // Set up framebuffers for each eye
    for (int eye = 0; eye < 2; eye++) {
        uint32_t width = g_vr_opengl.width[eye];
        uint32_t height = g_vr_opengl.height[eye];
        
        // Bind framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, g_vr_opengl.framebuffers[eye]);
        
        // Create and attach color texture
        glBindTexture(GL_TEXTURE_2D, g_vr_opengl.colorTextures[eye]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_vr_opengl.colorTextures[eye], 0);
        
        // Create and attach depth renderbuffer
        glBindRenderbuffer(GL_RENDERBUFFER, g_vr_opengl.depthRenderbuffers[eye]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24_OES, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_vr_opengl.depthRenderbuffers[eye]);

        // Check framebuffer completeness
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "Framebuffer incomplete for eye %d: 0x%x\n", eye, status);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            vr_opengl_shutdown();
            return 0;
        }
        
        printf("Created framebuffer for eye %d: FBO=%u, Color=%u, Depth=%u\n",
               eye, g_vr_opengl.framebuffers[eye], 
               g_vr_opengl.colorTextures[eye],
               g_vr_opengl.depthRenderbuffers[eye]);
    }
    
    // Unbind framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    
    // Create quad layer framebuffer for HUD
    vr_renderer_get_quad_dimensions(&g_vr_opengl.quadWidth, &g_vr_opengl.quadHeight);
    
    if (g_vr_opengl.quadWidth == 0 || g_vr_opengl.quadHeight == 0) {
        fprintf(stderr, "Invalid HUD quad layer dimensions\n");
        vr_opengl_shutdown();
        return 0;
    }
    
    printf("Initializing HUD quad layer framebuffer: %ux%u\n", g_vr_opengl.quadWidth, g_vr_opengl.quadHeight);
    
    // Generate quad framebuffer and texture
    glGenFramebuffers(1, &g_vr_opengl.quadFramebuffer);
    glGenTextures(1, &g_vr_opengl.quadColorTexture);
    
    // Bind and configure quad framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, g_vr_opengl.quadFramebuffer);
    
    // Create and attach color texture
    glBindTexture(GL_TEXTURE_2D, g_vr_opengl.quadColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_vr_opengl.quadWidth, g_vr_opengl.quadHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_vr_opengl.quadColorTexture, 0);
    
    // Check framebuffer completeness
    GLenum quadStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (quadStatus != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "HUD quad framebuffer incomplete: 0x%x\n", quadStatus);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        vr_opengl_shutdown();
        return 0;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    printf("Created HUD quad layer framebuffer: FBO=%u, Color=%u\n",
           g_vr_opengl.quadFramebuffer, g_vr_opengl.quadColorTexture);
    
    // Create DJUI layer framebuffer
    vr_renderer_get_djui_dimensions(&g_vr_opengl.djuiWidth, &g_vr_opengl.djuiHeight);
    
    if (g_vr_opengl.djuiWidth == 0 || g_vr_opengl.djuiHeight == 0) {
        fprintf(stderr, "Invalid DJUI quad layer dimensions\n");
        vr_opengl_shutdown();
        return 0;
    }
    
    printf("Initializing DJUI quad layer framebuffer: %ux%u\n", g_vr_opengl.djuiWidth, g_vr_opengl.djuiHeight);
    
    // Generate DJUI framebuffer and texture
    glGenFramebuffers(1, &g_vr_opengl.djuiFramebuffer);
    glGenTextures(1, &g_vr_opengl.djuiColorTexture);
    
    // Bind and configure DJUI framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, g_vr_opengl.djuiFramebuffer);
    
    // Create and attach color texture
    glBindTexture(GL_TEXTURE_2D, g_vr_opengl.djuiColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_vr_opengl.djuiWidth, g_vr_opengl.djuiHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_vr_opengl.djuiColorTexture, 0);
    
    // Check framebuffer completeness
    GLenum djuiStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (djuiStatus != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "DJUI quad framebuffer incomplete: 0x%x\n", djuiStatus);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        vr_opengl_shutdown();
        return 0;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    printf("Created DJUI quad layer framebuffer: FBO=%u, Color=%u\n",
           g_vr_opengl.djuiFramebuffer, g_vr_opengl.djuiColorTexture);
    
    g_vr_opengl.initialized = 1;
    printf("VR OpenGL integration initialized successfully\n");
    
    return 1;
}

void vr_opengl_shutdown(void)
{
    if (!g_vr_opengl.initialized) {
        return;
    }
    
    printf("Shutting down VR OpenGL integration...\n");
    
    // Delete framebuffers
    if (g_vr_opengl.framebuffers[0] != 0 || g_vr_opengl.framebuffers[1] != 0) {
        glDeleteFramebuffers(2, g_vr_opengl.framebuffers);
        g_vr_opengl.framebuffers[0] = 0;
        g_vr_opengl.framebuffers[1] = 0;
    }
    
    // Delete textures
    if (g_vr_opengl.colorTextures[0] != 0 || g_vr_opengl.colorTextures[1] != 0) {
        glDeleteTextures(2, g_vr_opengl.colorTextures);
        g_vr_opengl.colorTextures[0] = 0;
        g_vr_opengl.colorTextures[1] = 0;
    }
    
    // Delete depth renderbuffers
    if (g_vr_opengl.depthRenderbuffers[0] != 0 || g_vr_opengl.depthRenderbuffers[1] != 0) {
        glDeleteRenderbuffers(2, g_vr_opengl.depthRenderbuffers);
        g_vr_opengl.depthRenderbuffers[0] = 0;
        g_vr_opengl.depthRenderbuffers[1] = 0;
    }
    
    // Delete quad layer resources
    if (g_vr_opengl.quadFramebuffer != 0) {
        glDeleteFramebuffers(1, &g_vr_opengl.quadFramebuffer);
        g_vr_opengl.quadFramebuffer = 0;
    }
    
    if (g_vr_opengl.quadColorTexture != 0) {
        glDeleteTextures(1, &g_vr_opengl.quadColorTexture);
        g_vr_opengl.quadColorTexture = 0;
    }
    
    // Delete DJUI layer resources
    if (g_vr_opengl.djuiFramebuffer != 0) {
        glDeleteFramebuffers(1, &g_vr_opengl.djuiFramebuffer);
        g_vr_opengl.djuiFramebuffer = 0;
    }
    
    if (g_vr_opengl.djuiColorTexture != 0) {
        glDeleteTextures(1, &g_vr_opengl.djuiColorTexture);
        g_vr_opengl.djuiColorTexture = 0;
    }
    
    g_vr_opengl.initialized = 0;
    printf("VR OpenGL integration shutdown complete\n");
}

int vr_opengl_is_initialized(void)
{
    return g_vr_opengl.initialized;
}

int vr_opengl_begin_eye(int eye)
{
    if (!g_vr_opengl.initialized) {
        return 0;
    }
    
    if (eye < 0 || eye > 1) {
        fprintf(stderr, "Invalid eye index: %d\n", eye);
        return 0;
    }
    
    // Save current framebuffer binding
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_vr_opengl.previousFramebuffer);
    
    // Bind VR framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, g_vr_opengl.framebuffers[eye]);
    
    // Set viewport
    glViewport(0, 0, g_vr_opengl.width[eye], g_vr_opengl.height[eye]);
    
    g_vr_opengl.activeEye = eye;
    
    return 1;
}

void vr_opengl_end_eye(int eye)
{
    if (!g_vr_opengl.initialized || g_vr_opengl.activeEye != eye) {
        return;
    }

    // Render virtual keyboard
    if (openxr_is_keyboard_visible()) {
        openxr_render_keyboard(eye);
    }
    
    // Copy framebuffer to Vulkan swapchain image
    if (vr_copy_is_initialized()) {
        if (!vr_copy_framebuffer_to_swapchain(eye)) {
            static int warned = 0;
            if (!warned) {
                fprintf(stderr, "Warning: Failed to copy framebuffer to swapchain for eye %d\n", eye);
                warned = 1;
            }
        }
    }
    
    // Restore previous framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, g_vr_opengl.previousFramebuffer);
    
    g_vr_opengl.activeEye = -1;
}

unsigned int vr_opengl_get_framebuffer(int eye)
{
    if (!g_vr_opengl.initialized || eye < 0 || eye > 1) {
        return 0;
    }
    return g_vr_opengl.framebuffers[eye];
}

unsigned int vr_opengl_get_texture(int eye)
{
    if (!g_vr_opengl.initialized || eye < 0 || eye > 1) {
        return 0;
    }
    return g_vr_opengl.colorTextures[eye];
}

void vr_opengl_get_viewport(int eye, uint32_t* width, uint32_t* height)
{
    if (!g_vr_opengl.initialized || eye < 0 || eye > 1) {
        *width = 0;
        *height = 0;
        return;
    }
    
    *width = g_vr_opengl.width[eye];
    *height = g_vr_opengl.height[eye];
}

unsigned int vr_opengl_get_quad_framebuffer(void)
{
    if (!g_vr_opengl.initialized) {
        return 0;
    }
    return g_vr_opengl.quadFramebuffer;
}

unsigned int vr_opengl_get_djui_framebuffer(void)
{
    if (!g_vr_opengl.initialized) {
        return 0;
    }
    return g_vr_opengl.djuiFramebuffer;
}

void vr_opengl_render_djui_to_djui_quad(void)
{
    if (!g_vr_opengl.initialized) {
        return;
    }
    
    // Save current state
    GLint prevFBO;
    GLint prevViewport[4];
    GLboolean wasBlend;
    GLboolean wasDepth;
    GLboolean wasCull;
    
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    wasBlend = glIsEnabled(GL_BLEND);
    wasDepth = glIsEnabled(GL_DEPTH_TEST);
    wasCull = glIsEnabled(GL_CULL_FACE);
    
    // Bind DJUI framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, g_vr_opengl.djuiFramebuffer);
    glViewport(0, 0, g_vr_opengl.djuiWidth, g_vr_opengl.djuiHeight);
    
    // Set 2D render state for UI
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Clear to transparent
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Render DJUI
    extern void djui_render(void);
    
    // Save the current display list head
    Gfx* saved_head = gDisplayListHead;
    
    // Temporarily set gfx_current_dimensions to DJUI's widescreen resolution (320x180, 16:9)
    // while the viewport remains at the actual framebuffer size (640x360)
    // This causes DJUI to render at native widescreen resolution then GPU upscales it
    extern struct GfxDimensions gfx_current_dimensions;
    struct GfxDimensions saved_dimensions = gfx_current_dimensions;
    
    // Use DJUI's native widescreen resolution for layout calculations
    #define DJUI_WIDTH 320
    #define DJUI_HEIGHT 180
    gfx_current_dimensions.width = DJUI_WIDTH;
    gfx_current_dimensions.height = DJUI_HEIGHT;
    gfx_current_dimensions.aspect_ratio = (float)DJUI_WIDTH / (float)DJUI_HEIGHT;
    gfx_current_dimensions.x_adjust_4by3 = 0;
    gfx_current_dimensions.x_adjust_ratio = (4.0f / 3.0f) / gfx_current_dimensions.aspect_ratio;
    
    // Render DJUI commands to the display list
    djui_render();
    
    // Restore dimensions
    gfx_current_dimensions = saved_dimensions;
    
    // Terminate the temporary display list
    gSPEndDisplayList(gDisplayListHead++);
    
    // Execute the display list immediately to the currently bound DJUI framebuffer
    // This ensures it renders with the correct orthographic projection setup by djui_render
    // and without any VR perspective overrides (since we're not in the main render loop)
    // Use the immediate version to avoid triggering a full VR frame (WaitFrame/BeginFrame)
    gfx_run_commands_immediate(saved_head);
    
    // Restore the display list head so these commands are effectively removed from the main DL
    // This prevents them from being rendered again into the eye buffers
    gDisplayListHead = saved_head;
    
    // Copy DJUI framebuffer to Vulkan swapchain
    if (vr_copy_is_initialized()) {
        vr_copy_djui_to_swapchain();
    }
    
    // Restore state
    if (!wasBlend) glDisable(GL_BLEND);
    if (wasDepth)  glEnable(GL_DEPTH_TEST);
    if (wasCull)   glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

#endif // RAPI_GL

