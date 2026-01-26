#ifndef VR_OPENGL_H
#define VR_OPENGL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize VR OpenGL integration
// Creates framebuffers and textures for VR rendering
// Returns 1 on success, 0 on failure
int vr_opengl_init(void);

// Shutdown VR OpenGL integration
void vr_opengl_shutdown(void);

// Check if VR OpenGL is initialized
int vr_opengl_is_initialized(void);

// Begin rendering to an eye
// eye: 0 for left, 1 for right
// Binds the framebuffer for that eye
// Returns 1 on success, 0 on failure
int vr_opengl_begin_eye(int eye);

// End rendering to an eye
// Unbinds the framebuffer
void vr_opengl_end_eye(int eye);

// Get the framebuffer ID for an eye (for debugging)
unsigned int vr_opengl_get_framebuffer(int eye);

// Get the texture ID for an eye (for debugging/copying)
unsigned int vr_opengl_get_texture(int eye);

// Get the viewport dimensions for an eye
void vr_opengl_get_viewport(int eye, uint32_t* width, uint32_t* height);

// Get the quad framebuffer (HUD layer)
unsigned int vr_opengl_get_quad_framebuffer(void);

// Get the DJUI framebuffer
unsigned int vr_opengl_get_djui_framebuffer(void);

// Prepare the quad layer framebuffer by attaching the current swapchain texture
void vr_opengl_prepare_quad_layer(void);

// Prepare the DJUI layer framebuffer by attaching the current swapchain texture
void vr_opengl_prepare_djui_layer(void);

// This binds the DJUI framebuffer, clears it, and copies to Vulkan swapchain
void vr_opengl_render_djui_to_djui_quad(void);

#ifdef __cplusplus
}
#endif

#endif // VR_OPENGL_H

