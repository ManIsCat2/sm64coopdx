-- name: FOV and Camera Settings
-- description: UP and DOWN to change the fov, x to toggle FIRST PERSON and Y to reset the fov
--set true/false when enabled first person camera  configVrFirstPersonCamera

local fov = 60
local first_person = false
local fov_cooldown = 0
local fp_cooldown = 0

hook_event(HOOK_MARIO_UPDATE, function(m)
    if m.playerIndex ~= 0 then return end

    if fov_cooldown > 0 then
        fov_cooldown = fov_cooldown - 1
    end

    if fp_cooldown > 0 then
        fp_cooldown = fp_cooldown - 1
    end

    if fov_cooldown == 0 then
        if (m.controller.buttonDown & D_JPAD) ~= 0 then
            if fov > 5 then
                fov = fov - 5
                djui_chat_message_create("Current FOV: " .. tostring(fov))
                djui_chat_message_create("(Current FOV has been decreased!)")
            else
                djui_chat_message_create("\\#ff0000\\You have reached the limit!!")
            end
            fov_cooldown = 5
        elseif (m.controller.buttonDown & U_JPAD) ~= 0 then
            if fov < 170 then
                fov = fov + 5
                djui_chat_message_create("Current FOV: " .. tostring(fov))
                djui_chat_message_create("(Current FOV has been increased!)")
            else
                djui_chat_message_create("\\#ff0000\\You have reached the limit!!")
            end
            fov_cooldown = 5
        end
    end

    fov = math.min(math.max(fov, 5), 170)

    if first_person == true then
        set_first_person_enabled(true)
        gFirstPersonCamera.fov = fov
    else
        set_first_person_enabled(false)
    end
    
    if fp_cooldown == 0 and (m.controller.buttonPressed & X_BUTTON) ~= 0 then
        first_person = not first_person
        fp_cooldown = 120
    elseif fp_cooldown ~= 0 and (m.controller.buttonPressed & X_BUTTON) ~= 0 then
        local seconds = fp_cooldown / 30
        if seconds < 1 then
            seconds = math.floor(seconds * 10 + 0.5) / 10
            djui_chat_message_create("\\#ff0000\\Wait more \\#ffffff\\" .. tostring(seconds) .. "s \\#ff0000\\to switch!!")
        else
            djui_chat_message_create("\\#ff0000\\Wait more \\#ffffff\\" .. tostring(math.floor(seconds + 0.5)) .. "s \\#ff0000\\to switch!!")
        end
    end
    
    if (m.controller.buttonPressed & Y_BUTTON) ~= 0 then
        djui_chat_message_create("\\#00ff00\\FOV has been reset!")
        fov = 60
    end

    set_override_fov(fov)
end)