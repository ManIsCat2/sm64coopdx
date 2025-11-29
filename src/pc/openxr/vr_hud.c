#include "vr_hud.h"
#include "vr_opengl.h"
#include "vr_copy.h"
#include "vr_renderer.h"
#include "game/game_init.h"
#include "game/area.h"
#include "game/ingame_menu.h"
#include "pc/gfx/gfx_pc.h"
#include "pc/network/network.h"
#include "pc/djui/djui.h"
#include "pc/nametags.h"
#include "pc/lua/smlua_hooks.h"

#ifdef RAPI_GL

#include <stdio.h>

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

// External functions from game
extern void render_hud(void);
extern s16 render_menus_and_dialogs(void);
extern void render_text_labels(void);
extern void do_cutscene_handler(void);
extern void print_displaying_credits_entry(void);
extern void gfx_run_commands_immediate(Gfx *commands);
extern void print_act_selector_strings(void);

// External variables
extern Gfx *gDisplayListHead;
extern bool gDjuiDisabled;
extern bool gDjuiInMainMenu;
extern s16 gPauseScreenMode;
extern s16 gSaveOptSelectIndex;
extern struct Area *gCurrentArea;
extern u8 gOverrideHideActSelectHud;
extern s16 gCurrCourseNum;
extern s16 gCurrActNum;

void vr_render_hud_to_quad(void) {
    if (!vr_opengl_is_initialized()) {
        return;
    }
    
    // Save current OpenGL state
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
    
    // Get quad framebuffer
    unsigned int quadFBO = vr_opengl_get_quad_framebuffer();
    if (quadFBO == 0) {
        fprintf(stderr, "Warning: Quad framebuffer not available\n");
        return;
    }
    
    // Bind quad framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, quadFBO);
    
    // Get quad dimensions
    uint32_t quadWidth, quadHeight;
    vr_renderer_get_quad_dimensions(&quadWidth, &quadHeight);
    glViewport(0, 0, quadWidth, quadHeight);
    
    // Set 2D render state for HUD
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Clear to transparent
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Save the current display list head
    Gfx* saved_head = gDisplayListHead;
    
    // Render HUD elements
    if (!gDjuiDisabled) {
        djui_reset_hud_params();
        create_dl_ortho_matrix();
        djui_gfx_displaylist_begin();
        if (gServerSettings.nametags && !gDjuiInMainMenu) {
            nametags_render();
        }
        smlua_call_event_hooks(HOOK_ON_HUD_RENDER_BEHIND, djui_reset_hud_params);
        djui_gfx_displaylist_end();
    }
    
    render_hud();
    render_text_labels();
    do_cutscene_handler();
    if (!gDjuiInMainMenu) {
        print_displaying_credits_entry();
    }
    
    // Render star select menu only when in star select screen (course selected but not loaded yet)
    // gCurrCourseNum > 0 means we've selected a course, gCurrActNum == 0 means we haven't loaded into it yet
    // needs fix for bowser levels and other levels without a select screen
    if (gCurrCourseNum > 0 && gCurrActNum == 0) {
        print_act_selector_strings();
    }
    
    render_menus_and_dialogs();
    if (gPauseScreenMode != 0) {
        gSaveOptSelectIndex = gPauseScreenMode;
    }
    
    // Terminate the display list
    gSPEndDisplayList(gDisplayListHead++);
    
    // Execute the display list immediately to the quad framebuffer
    gfx_run_commands_immediate(saved_head);
    
    // Restore the display list head
    gDisplayListHead = saved_head;
    
    // Copy quad framebuffer to Vulkan swapchain
    if (vr_copy_is_initialized()) {
        vr_copy_quad_to_swapchain();
    }
    
    // Restore OpenGL state
    if (!wasBlend) glDisable(GL_BLEND);
    if (wasDepth)  glEnable(GL_DEPTH_TEST);
    if (wasCull)   glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

#endif // RAPI_GL
