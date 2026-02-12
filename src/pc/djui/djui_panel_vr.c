#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_vr.h"
#include "pc/utils/misc.h"
#include "pc/configfile.h"

static struct DjuiSelectionbox* sSnapAngleSelection = NULL;

static void djui_panel_vr_turn_mode_change(UNUSED struct DjuiBase* caller) {
    if (sSnapAngleSelection) {
        djui_base_set_enabled(&sSnapAngleSelection->base, configVrTurnMode != 0);
    }
}

void djui_panel_vr_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(VR, VR), false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        djui_checkbox_create(body, DLANG(VR, FIRST_PERSON_CAMERA), &configVrFirstPersonCamera, NULL);

        char* turnModeChoices[2] = { DLANG(VR, TURN_MODE_CONTINUOUS), DLANG(VR, TURN_MODE_SNAP) };
        djui_selectionbox_create(body, DLANG(VR, TURN_MODE), turnModeChoices, 2, &configVrTurnMode, djui_panel_vr_turn_mode_change);

        char* snapAngleChoices[12] = { "15", "30", "45", "60", "75", "90", "105", "120", "135", "150", "165", "180" };
        sSnapAngleSelection = djui_selectionbox_create(body, DLANG(VR, SNAP_ANGLE), snapAngleChoices, 6, &configVrSnapAngle, NULL);
        
        djui_panel_vr_turn_mode_change(NULL); // Apply initial state

        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    }
    
    djui_panel_add(caller, panel, NULL);
}
