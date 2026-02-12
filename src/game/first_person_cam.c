#include "first_person_cam.h"

#include "sm64.h"
#include "behavior_data.h"
#include "camera.h"
#include "level_update.h"
#include "object_list_processor.h"
#include "object_helpers.h"
#include "mario.h"
#include "hardcoded.h"
#include "save_file.h"

#include "engine/math_util.h"

#include "pc/controller/controller_mouse.h"
#include "pc/djui/djui.h"
#include "pc/djui/djui_hud_utils.h"
#include "pc/lua/utils/smlua_camera_utils.h"
#include "pc/lua/smlua_hooks.h"

#include "pc/configfile.h"
#ifdef OPENXR_ENABLED
#include "pc/openxr/vr_camera.h"
#endif

struct FirstPersonCamera gFirstPersonCamera = {
    .enabled = false,
    .forcePitch = false,
    .forceYaw = false,
    .forceRoll = true,
    .centerL = true,
    .pitch = 0,
    .yaw = 0,
    .crouch = 0,
    .fov = FIRST_PERSON_DEFAULT_FOV,
    .offset = { 0, 0, 0 }
};

extern s16 gMenuMode;

bool first_person_check_cancels(struct MarioState *m) {
    if (m->action == ACT_FIRST_PERSON || m->action == ACT_IN_CANNON || m->action == ACT_READING_NPC_DIALOG || m->action == ACT_DISAPPEARED || m->action == ACT_FLYING) {
        return true;
    }
    if (find_object_with_behavior(smlua_override_behavior(bhvActSelector)) != NULL) { return true; }

    if (gLuaLoadingMod != NULL) { return false; }

    struct Object *bowser = find_object_with_behavior(smlua_override_behavior(bhvBowser));
    if ((gCurrLevelNum == LEVEL_BOWSER_1 || gCurrLevelNum == LEVEL_BOWSER_2 || gCurrLevelNum == LEVEL_BOWSER_3) &&
        bowser != NULL &&
        (bowser->oAction == 5 || bowser->oAction == 6)) {
        return true;
    }

    return false;
}

bool get_first_person_enabled(void) {
    return gFirstPersonCamera.enabled && !first_person_check_cancels(&gMarioStates[0]);
}

static bool sOriginalInvertRightX = false;
static bool sStoredInvertRightX = false;

void set_first_person_enabled(bool enable) {
    gFirstPersonCamera.enabled = enable;

    if (enable) {
#ifdef OPENXR_ENABLED
        configVrFirstPersonCamera = true;
#endif

        if (!sStoredInvertRightX) {
            sOriginalInvertRightX = configStick.invertRightX;
            sStoredInvertRightX = true;
        }
        configStick.invertRightX = true;
    } else {
#ifdef OPENXR_ENABLED
        configVrFirstPersonCamera = false;
#endif
        if (sStoredInvertRightX) {
            configStick.invertRightX = sOriginalInvertRightX;
            sStoredInvertRightX = false;
        }
    }
}

static void first_person_camera_update(void) {
    struct MarioState *m = &gMarioStates[0];
    f32 sensX = 0.3f * camera_config_get_x_sensitivity();
    f32 sensY = 0.4f * camera_config_get_y_sensitivity();

    gFirstPersonCamera.yaw = vr_camera_get_yaw();

    if (mouse_relative_enabled) {
        // hack: make c buttons work for moving the camera
        s16 extStickX = m->controller->extStickX;
        s16 extStickY = m->controller->extStickY;
        if (extStickX == 0) {
            extStickX = (clamp(m->controller->buttonDown & R_CBUTTONS, 0, 1) - clamp(m->controller->buttonDown & L_CBUTTONS, 0, 1)) * 32;
        }
        if (extStickY == 0) {
            extStickY = (clamp(m->controller->buttonDown & U_CBUTTONS, 0, 1) - clamp(m->controller->buttonDown & D_CBUTTONS, 0, 1)) * 24;
        }

#ifndef OPENXR_ENABLED
        if (!gFirstPersonCamera.forcePitch) {
            gFirstPersonCamera.pitch -= sensY * (extStickY - 1.5f * mouse_y);
            gFirstPersonCamera.pitch = clamp(gFirstPersonCamera.pitch, -0x3F00, 0x3F00);
        }
#endif

        // update yaw
        if (!gFirstPersonCamera.forceYaw) {
            if (m->controller->buttonDown & L_TRIG && gFirstPersonCamera.centerL) {
                gFirstPersonCamera.yaw = m->faceAngle[1] + 0x8000;
                vr_camera_reset_yaw_offset(gFirstPersonCamera.yaw);
            } else {
#ifdef OPENXR_ENABLED
            if (configVrFirstPersonCamera) {
                if (configVrTurnMode == 0) { // Smooth turn
                    s16 yawDelta = sensX * (extStickX - 1.5f * mouse_x);
                    vr_camera_add_yaw_offset(yawDelta);
                    gFirstPersonCamera.yaw += yawDelta;
                } else { // Snap turn
                    static bool sSnapReady = true;
                    if (ABS(extStickX) < 16) {
                        sSnapReady = true;
                    } else if (sSnapReady && ABS(extStickX) > 64) {
                        s16 snapAngle = (configVrSnapAngle + 1) * (0x10000 / 24); // 15 degrees * (index + 1)
                        if (extStickX > 0) {
                            vr_camera_add_yaw_offset(snapAngle);
                            gFirstPersonCamera.yaw += snapAngle;
                        } else {
                            // Right Push (Negative due to inversion) -> Turn Right (Decrease Yaw)
                            vr_camera_add_yaw_offset(-snapAngle);
                            gFirstPersonCamera.yaw -= snapAngle;
                        }
                        sSnapReady = false;
                    }
                }
            } else {
                s16 yawDelta = sensX * (extStickX - 1.5f * mouse_x);
                gFirstPersonCamera.yaw += yawDelta;
            }
#else
            s16 yawDelta = sensX * (extStickX - 1.5f * mouse_x);
            gFirstPersonCamera.yaw += yawDelta;
#endif
            }
        }
    }

    // fix yaw for some specific actions
    // if the left stick is held, use Mario's yaw to set the camera's yaw
    // otherwise, set Mario's yaw to the camera's yaw
    u32 actions[] = { ACT_FLYING, ACT_HOLDING_BOWSER, ACT_TORNADO_TWIRLING, ACT_FLAG_ON_POLE, ACT_FLAG_SWIMMING, ACT_FLAG_SWIMMING_OR_FLYING };
    for (s32 i = 0; i < 6; i++) {
        u32 flag = actions[i];
        if ((m->action & flag) == flag) {
            if (ABS(m->controller->stickX) > 4) {
#ifdef OPENXR_ENABLED
                if (!configVrFirstPersonCamera)
#endif
                gFirstPersonCamera.yaw = m->faceAngle[1] + 0x8000;
            } else {
                m->faceAngle[1] = gFirstPersonCamera.yaw - 0x8000;
            }
            break;
        }
    }
    if (m->action == ACT_LEDGE_GRAB) {
#ifdef OPENXR_ENABLED
        if (!configVrFirstPersonCamera)
#endif
        gFirstPersonCamera.yaw = m->faceAngle[1] + 0x8000;
    }

    gLakituState.yaw = gFirstPersonCamera.yaw;
    m->area->camera->yaw = gFirstPersonCamera.yaw;

    // update crouch
    if (mario_is_crouching(m) || m->action == ACT_LEDGE_GRAB) {
        bool up = (m->controller->buttonDown & Z_TRIG) != 0 || m->action == ACT_CROUCH_SLIDE || m->action == ACT_LEDGE_GRAB;
        f32 inc = 10 * (up ? 1 : -1);
        gFirstPersonCamera.crouch = clamp(gFirstPersonCamera.crouch + inc, 0, FIRST_PERSON_MARIO_HEAD_POS - FIRST_PERSON_MARIO_HEAD_POS_SHORT);
    } else {
        gFirstPersonCamera.crouch = clamp(gFirstPersonCamera.crouch - 10, 0, FIRST_PERSON_MARIO_HEAD_POS - FIRST_PERSON_MARIO_HEAD_POS_SHORT);
    }

    if (m->action == ACT_LEDGE_GRAB) {
        gFirstPersonCamera.crouch = FIRST_PERSON_MARIO_HEAD_POS - FIRST_PERSON_MARIO_HEAD_POS_SHORT;
    }

    // update pos
    gLakituState.pos[0] = (m->pos[0] + gFirstPersonCamera.offset[0]) + coss(gFirstPersonCamera.pitch) * sins(gFirstPersonCamera.yaw);
    gLakituState.pos[1] = (m->pos[1] + gFirstPersonCamera.offset[1]) + sins(gFirstPersonCamera.pitch) + (FIRST_PERSON_MARIO_HEAD_POS - gFirstPersonCamera.crouch);
    gLakituState.pos[2] = (m->pos[2] + gFirstPersonCamera.offset[2]) + coss(gFirstPersonCamera.pitch) * coss(gFirstPersonCamera.yaw);
    vec3f_copy(m->area->camera->pos, gLakituState.pos);
    vec3f_copy(gLakituState.curPos, gLakituState.pos);
    vec3f_copy(gLakituState.goalPos, gLakituState.pos);

    // update focus
    gLakituState.focus[0] = (m->pos[0] + gFirstPersonCamera.offset[0]) - 100 * coss(gFirstPersonCamera.pitch) * sins(gFirstPersonCamera.yaw);
    gLakituState.focus[1] = (m->pos[1] + gFirstPersonCamera.offset[1]) - 100 * sins(gFirstPersonCamera.pitch) + (FIRST_PERSON_MARIO_HEAD_POS - gFirstPersonCamera.crouch);
    gLakituState.focus[2] = (m->pos[2] + gFirstPersonCamera.offset[2]) - 100 * coss(gFirstPersonCamera.pitch) * coss(gFirstPersonCamera.yaw);
    vec3f_copy(m->area->camera->focus, gLakituState.focus);
    vec3f_copy(gLakituState.curFocus, gLakituState.focus);
    vec3f_copy(gLakituState.goalFocus, gLakituState.focus);

    // set other values
    if (gFirstPersonCamera.forceRoll) {
        gLakituState.roll = 0;
    }
    gLakituState.posHSpeed = 0;
    gLakituState.posVSpeed = 0;
    gLakituState.focHSpeed = 0;
    gLakituState.focVSpeed = 0;
    vec3s_set(gLakituState.shakeMagnitude, 0, 0, 0);
}

void first_person_update(void) {
    if (gFirstPersonCamera.enabled && !gDjuiInMainMenu) {
        struct MarioState *m = &gMarioStates[0];

        // check cancels
        bool cancel = first_person_check_cancels(m);
        if (cancel) { return; }

        if (m->action == ACT_SHOT_FROM_CANNON && m->area->camera->mode == CAMERA_MODE_INSIDE_CANNON) {
            gFirstPersonCamera.yaw = m->faceAngle[1] + 0x8000;
            m->area->camera->mode = CAMERA_MODE_FREE_ROAM;
        }

        if (gFirstPersonCamera.pitch <= -0x3F00 &&
            m->floor && m->floor->type == SURFACE_LOOK_UP_WARP &&
            save_file_get_total_star_count(gCurrSaveFileNum - 1, COURSE_MIN - 1, COURSE_MAX - 1) >= gLevelValues.wingCapLookUpReq &&
            m->forwardVel == 0 &&
            sCurrPlayMode != PLAY_MODE_PAUSED) {
            level_trigger_warp(m, WARP_OP_LOOK_UP);
        }

        m->marioBodyState->modelState = 0x100;
        if (m->heldObj) {
            Vec3f camDir = {
                m->area->camera->focus[0] - m->area->camera->pos[0],
                m->area->camera->focus[1] - m->area->camera->pos[1],
                m->area->camera->focus[2] - m->area->camera->pos[2]
            };
            vec3f_normalize(camDir);
            vec3f_mul(camDir, 100);
            vec3f_sum(m->marioObj->header.gfx.pos, m->pos, camDir);
        }

        first_person_camera_update();
    }
}

void first_person_reset(void) {
    gFirstPersonCamera.forceRoll = false;
    gFirstPersonCamera.centerL = true;
    gFirstPersonCamera.pitch = 0;
    gFirstPersonCamera.yaw = 0;
    gFirstPersonCamera.crouch = 0;
    gFirstPersonCamera.fov = FIRST_PERSON_DEFAULT_FOV;
    gFirstPersonCamera.offset[0] = 0;
    gFirstPersonCamera.offset[1] = 0;
    gFirstPersonCamera.offset[2] = 0;
}
