#include <stdio.h>

#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_save_storage.h"
#include "game/save_file.h"
#include "pc/save_location_config.h"
#include "pc/save_location.h"

static struct DjuiInputbox *sSaveStorageInputBox = NULL;
static struct DjuiText *sSaveStorageStatusText = NULL;

static void djui_panel_save_storage_status(const char *message) {
    if (sSaveStorageStatusText != NULL) {
        djui_text_set_text(sSaveStorageStatusText, message);
    }
}

// shows a localized status message with '@' replaced by `value`
static void djui_panel_save_storage_status_replace(const char *key, const char *value) {
    static char buffer[MAX_SAVE_LOCATION_STRING + 256];
    djui_language_replace(djui_language_get("SAVE_STORAGE", key), buffer, sizeof(buffer), '@',
                          (char *) value);
    djui_panel_save_storage_status(buffer);
}

static void djui_panel_save_storage_applied(const char *dir) {
    djui_panel_save_storage_status_replace("APPLIED", dir);
    // reload the save state (EEPROM -> save buffer) and refresh star counts
    save_file_reload(TRUE);
    djui_inputbox_set_text(sSaveStorageInputBox, configSaveLocation);
}

static void djui_panel_save_storage_apply(UNUSED struct DjuiBase *caller) {
    if (!gDjuiInMainMenu) {
        djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_UNAVAILABLE));
        return;
    }

    if (sSaveStorageInputBox == NULL) {
        return;
    }

    // typing only stages the value; the live path changes here, on apply
    char dir[MAX_SAVE_LOCATION_STRING];
    snprintf(dir, MAX_SAVE_LOCATION_STRING, "%s", sSaveStorageInputBox->buffer);

    switch (save_location_set(dir)) {
        case SAVE_LOCATION_OK:
            djui_panel_save_storage_applied(dir);
            return;
        case SAVE_LOCATION_ERR_EMPTY:
            djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_EMPTY));
            return;
        case SAVE_LOCATION_ERR_RELATIVE:
            djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_RELATIVE));
            return;
        case SAVE_LOCATION_ERR_BAD_CHAR:
            djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_BAD_CHAR));
            return;
        case SAVE_LOCATION_ERR_TOO_LONG:
            djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_TOO_LONG));
            return;
        case SAVE_LOCATION_ERR_MKDIR:
            djui_panel_save_storage_status_replace("ERR_MKDIR", dir);
            return;
        case SAVE_LOCATION_ERR_COPY:
            djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_COPY));
            return;
        case SAVE_LOCATION_ERR_CONFIG:
            djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_CONFIG));
            return;
        case SAVE_LOCATION_ERR_CONFLICT:
            // unreachable from save_location_set today; kept for completeness
            djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_CONFLICT));
            return;
    }
}

static void djui_panel_save_storage_reset(UNUSED struct DjuiBase *caller) {
    if (!gDjuiInMainMenu) {
        djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_UNAVAILABLE));
        return;
    }

    enum save_location_result result = save_location_reset_to_default();
    if (result != SAVE_LOCATION_OK) {
        // The custom path stays active; explain what actually went wrong.
        switch (result) {
            case SAVE_LOCATION_ERR_CONFIG:
                djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_CONFIG));
                return;
            case SAVE_LOCATION_ERR_CONFLICT:
                djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_CONFLICT));
                return;
            default:
                djui_panel_save_storage_status(DLANG(SAVE_STORAGE, ERR_RESET));
                return;
        }
    }

    djui_panel_save_storage_status(DLANG(SAVE_STORAGE, RESET_DONE));
    save_file_reload(TRUE);
    if (sSaveStorageInputBox != NULL) {
        djui_inputbox_set_text(sSaveStorageInputBox, configSaveLocation);
    }
}

void djui_panel_save_storage_create(struct DjuiBase *caller) {
    sSaveStorageStatusText = NULL;

    struct DjuiThreePanel *panel =
        djui_panel_menu_create(DLANG(SAVE_STORAGE, SAVE_STORAGE_TITLE), false);
    struct DjuiBase *body = djui_three_panel_get_body(panel);
    {
        struct DjuiText *text1 = djui_text_create(body, DLANG(SAVE_STORAGE, DIRECTORY));
        djui_base_set_size_type(&text1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&text1->base, 1.0f, 100);
        djui_base_compute_tree(&text1->base);
        u16 lines = djui_text_count_lines(text1, 12);
        f32 textHeight = 32 * 0.8125f * lines + 8;
        djui_base_set_size(&text1->base, 1.0f, textHeight);
        djui_base_set_color(&text1->base, 220, 220, 220, 255);

        sSaveStorageInputBox = djui_inputbox_create(body, MAX_SAVE_LOCATION_STRING);
        djui_base_set_size_type(&sSaveStorageInputBox->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&sSaveStorageInputBox->base, 1.0f, 32);
        djui_inputbox_set_text(sSaveStorageInputBox, configSaveLocation);

        struct DjuiRect *rect1 = djui_rect_container_create(body, 32);
        {
            struct DjuiButton *button1 =
                djui_button_create(&rect1->base, DLANG(SAVE_STORAGE, APPLY), DJUI_BUTTON_STYLE_NORMAL,
                                   djui_panel_save_storage_apply);
            djui_base_set_size(&button1->base, 0.485f, 32);
            djui_base_set_alignment(&button1->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);

            struct DjuiButton *button2 =
                djui_button_create(&rect1->base, DLANG(SAVE_STORAGE, RESET), DJUI_BUTTON_STYLE_NORMAL,
                                   djui_panel_save_storage_reset);
            djui_base_set_size(&button2->base, 0.485f, 32);
            djui_base_set_alignment(&button2->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);
        }

        struct DjuiRect *rect2 = djui_rect_container_create(body, 86);
        {
            sSaveStorageStatusText = djui_text_create(&rect2->base, "");
            djui_base_set_size_type(&sSaveStorageStatusText->base, DJUI_SVT_RELATIVE,
                                    DJUI_SVT_ABSOLUTE);
            djui_base_set_size(&sSaveStorageStatusText->base, 1.0f, 86);
            djui_base_set_color(&sSaveStorageStatusText->base, 220, 220, 220, 255);
        }

        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    }

    djui_panel_add(caller, panel, NULL);
}
