// vr_ui.c
#include <stdint.h>           // for uint32_t
#include <stdbool.h>          // for bool
#include <stdio.h>            // for printf, fprintf

// fix for android 
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "pc/djui/djui.h"             // your DJUI declarations
#include "pc/gfx/gfx_window_manager_api.h"  // for gfx_get_dimensions() etc.
#include "pc/debuglog.h"
#include "pc/pc_main.h"

// Provide these if you need to override window dims for DJUI scaling
extern void djui_frame_begin(void);   // whatever your DJUI “begin frame” entry is
extern void djui_frame_end(void);     // optional, if your build uses it
extern void djui_render(void);        // top-level DJUI render (menus/HUD)

// Optional: expose an override so gfx_get_dimensions() returns eye size during this pass
void gfx_push_dim_override(uint32_t w, uint32_t h);
void gfx_pop_dim_override(void);

static void vr_draw_gui_into_eye(GLuint fbo, uint32_t width, uint32_t height)
{
    // Save state
    GLint prevFBO = 0;
    GLint prevViewport[4] = {0};
    GLboolean wasBlend = glIsEnabled(GL_BLEND);
    GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean wasCull  = glIsEnabled(GL_CULL_FACE);

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    // Bind the eye FBO & a full-size viewport
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, (GLint)width, (GLint)height);

    // 2D UI render state
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Make DJUI use the eye dimensions so its scaling math is correct
    gfx_push_dim_override(width, height);

    // If your DJUI needs a 2D ortho, do it here (only if your backend doesn’t set it):
    // glMatrixMode(GL_PROJECTION); glLoadIdentity();
    // glOrtho(0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, -1, 1);
    // glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    // Kick DJUI
    if (djui_frame_begin) djui_frame_begin();
    djui_render();
    if (djui_frame_end) djui_frame_end();

    gfx_pop_dim_override();

    // Restore state
    if (!wasBlend) glDisable(GL_BLEND);
    if (wasDepth)  glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (wasCull)   glEnable(GL_CULL_FACE);   else glDisable(GL_CULL_FACE);

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
}