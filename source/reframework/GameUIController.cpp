#include "GameUIController.hpp"
#include "reframework/API.hpp"
#include "ModSettings.hpp"
#include <memory>

using namespace reframework;

std::unique_ptr<GameUIController> game_ui_controller_instance = nullptr;

bool game_ui_controller_on_pre_gui_draw_element(void *obj_void, void* context) {
    if (game_ui_controller_instance == nullptr) {
        return true;
    }

    // Return true to draw the UI
    if (game_ui_controller_instance->is_in_hide()) {
        return false;
    }

    auto settings = ModSettings::get_instance();

    // Check for our lovely notification icon
    if (game_ui_controller_instance->get_is_in_quest_result() && settings->hide_chat_notification) {
        auto obj = reinterpret_cast<reframework::API::ManagedObject*>(obj_void);
        auto game_obj = obj->call("get_GameObject", context, obj);

        if (game_obj == game_ui_controller_instance->get_notification_gameobject()) {
            return false;
        }
    }

    return true;
}

GameUIController::GameUIController(const REFrameworkPluginInitializeParam* initialize_params) {
    if (initialize_params != nullptr) {
        initialize_params->functions->on_pre_gui_draw_element(game_ui_controller_on_pre_gui_draw_element);
    }

    auto &api = reframework::API::get();
    auto tdb = api->tdb();

    auto on_awake_chat = tdb->find_method("app.GUI000020", "guiAwake");
    on_awake_chat->add_hook(pre_gui000020_on_gui_awake, post_gui000020_on_gui_awake, false);

    auto graphics_manager = api->get_managed_singleton("app.GraphicsManager");

    if (!graphics_manager) {
        api->log_error("Failed to get GraphicsManager singleton");
    } else {
        auto vm_context = api->get_vm_context();

        display_settings = graphics_manager->call<reframework::API::ManagedObject*>("get_DisplaySettings", vm_context,
            graphics_manager);

        if (!display_settings) {
            api->log_error("Failed to get DisplaySettings from GraphicsManager singleton");
        } else {
            auto display_settings_type = display_settings->get_type_definition();

            display_settings_update_request = display_settings_type->find_method("updateRequest");
            display_settings_get_UseSDRBrightnessOptionForOverlay = display_settings_type->find_method("get_UseSDRBrightnessOptionForOverlay");
            display_settings_set_UseSDRBrightnessOptionForOverlay = display_settings_type->find_method("set_UseSDRBrightnessOptionForOverlay");
            display_settings_get_gamma_for_overlay = display_settings_type->find_method("get_GammaForOverlay");
            display_settings_get_output_lower_limit_for_overlay = display_settings_type->find_method("get_OutputLowerLimitForOverlay");
            display_settings_get_output_upper_limit_for_overlay = display_settings_type->find_method("get_OutputUpperLimitForOverlay");
            display_settings_set_gamma_for_overlay = display_settings_type->find_method("set_GammaForOverlay");
            display_settings_set_output_lower_limit_for_overlay = display_settings_type->find_method("set_OutputLowerLimitForOverlay");
            display_settings_set_output_upper_limit_for_overlay = display_settings_type->find_method("set_OutputUpperLimitForOverlay");

            if (!display_settings_update_request || !display_settings_get_UseSDRBrightnessOptionForOverlay || !display_settings_set_UseSDRBrightnessOptionForOverlay ||
                !display_settings_get_gamma_for_overlay || !display_settings_get_output_lower_limit_for_overlay || !display_settings_get_output_upper_limit_for_overlay ||
                !display_settings_set_gamma_for_overlay || !display_settings_set_output_lower_limit_for_overlay || !display_settings_set_output_upper_limit_for_overlay) {
                api->log_error("Failed to find methods in DisplaySettings class");
            }
        }
    }

    auto app_option_util_type = tdb->find_type("app.OptionUtil");
    app_option_util_is_hdr_enabled = app_option_util_type->find_method("isHdrEnabled");

    if (!app_option_util_is_hdr_enabled) {
        api->log_error("Failed to get methods from OptionUtil type");
    }

    auto gui_system_module_option_type = tdb->find_type("app.cGUISystemModuleOption");

    gui_system_module_option_on_update = gui_system_module_option_type->find_method("onUpdate");
    gui_system_module_option_apply_setting = gui_system_module_option_type->find_method("applySetting(app.Option.ID, System.Int32, app.cGUISystemModuleOption.EXCLUDE_FLAG)");
    gui_system_module_option_get_value = gui_system_module_option_type->find_method("getValue");

    if (!gui_system_module_option_on_update || !gui_system_module_option_apply_setting || !gui_system_module_option_get_value) {
        api->log_error("Failed to get methods from cGUISystemModuleOption type");
    }

    gui_system_module_option_on_update->add_hook(pre_gui_system_module_option_on_update, post_gui_system_module_option_on_update, false);
}

void GameUIController::hide_for(int frame_count) {
    if (is_in_hide()) {
        API::get()->log_info("Already hiding, ignoring request to hide for %d frames", frame_count);
        return;
    } else {
        hide_frames_passed = 0;
        total_hide_frame = frame_count;
    }
}

void GameUIController::show_impl() {
    if (is_in_hide()) {
        hide_frames_passed = -1;
    } else {
        API::get()->log_info("Not hiding, ignoring request to show");
    }
}

float GameUIController::get_hiding_progress() const {
    if (!is_in_hide() || is_hiding_until_notice()) {
        return 0.0f;
    }

    return static_cast<float>(hide_frames_passed) / static_cast<float>(total_hide_frame);
}

GameUIController *GameUIController::get_instance() {
    return game_ui_controller_instance ? game_ui_controller_instance.get() : nullptr;
}

void GameUIController::initialize(const REFrameworkPluginInitializeParam *initialize_params) {
    if (game_ui_controller_instance == nullptr) {
        game_ui_controller_instance = std::unique_ptr<GameUIController>(new GameUIController(initialize_params));
    }
}

void GameUIController::update() {
    if (is_in_hide()) {
        if (!is_hiding_until_notice()) {
            if (hide_frames_passed > total_hide_frame) {
                show_impl();
            }
        }
    }
}

int GameUIController::pre_gui000020_on_gui_awake(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (game_ui_controller_instance) {
        auto gui_comp = reinterpret_cast<reframework::API::ManagedObject*>(argv[1]);
        auto game_object = gui_comp->call<reframework::API::ManagedObject*>("get_GameObject", argv[0], gui_comp);

        game_ui_controller_instance->notification_GO = game_object;
    }

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void GameUIController::post_gui000020_on_gui_awake(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {

}

int GameUIController::pre_gui_system_module_option_on_update(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    auto vm_context = reframework::API::get()->get_vm_context();
    auto gui_system_module_option = reinterpret_cast<reframework::API::ManagedObject*>(argv[1]);

    if (gui_system_module_option == nullptr) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    auto game_ui_controller = GameUIController::get_instance();
    game_ui_controller->gui_system_module_option = gui_system_module_option;

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void GameUIController::post_gui_system_module_option_on_update(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
}

void GameUIController::backup_necessary_display_settings() {
    auto &api = reframework::API::get();
    auto vm_context = api->get_vm_context();

    if (gui_system_module_option == nullptr) {
        api->log_error("gui_system_module_option is null, can't backup brightness settings");
        return;
    }

    sdr_gamma = gui_system_module_option_get_value->call<int>(vm_context, gui_system_module_option, BRIGHTNESS_SDR_GAMMA_OPTION_ID);
    sdr_output_lower_limit = gui_system_module_option_get_value->call<int>(vm_context, gui_system_module_option, BRIGHTNESS_SDR_LIMIT_LOWER_OPTION_ID);
    sdr_output_upper_limit = gui_system_module_option_get_value->call<int>(vm_context, gui_system_module_option, BRIGHTNESS_SDR_LIMIT_UPPER_OPTION_ID);
    hdr_highlight = gui_system_module_option_get_value->call<int>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_HIGHLIGHT_OPTION_ID);
    hdr_shadow = gui_system_module_option_get_value->call<int>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_SHADOW_OPTION_ID);
    hdr_max_nits = gui_system_module_option_get_value->call<int>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_MAX_NITS_OPTION_ID);
    hdr_white_nits = gui_system_module_option_get_value->call<int>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_WHITE_NITS_OPTION_ID);
    hdr_white_nits_for_overlay = gui_system_module_option_get_value->call<int>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_UI_WHITE_NITS_OPTION_ID);
    hdr_saturation = gui_system_module_option_get_value->call<int>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_SATURATION_OPTION_ID);

    use_sdr_brightness_option_for_overlay = display_settings_get_UseSDRBrightnessOptionForOverlay->call<bool>(vm_context, display_settings);
    sdr_display_settings_ui_gamma = display_settings_get_gamma_for_overlay->call<float>(vm_context, display_settings);
    sdr_display_settings_output_lower_limit = display_settings_get_output_lower_limit_for_overlay->call<float>(vm_context, display_settings);
    sdr_display_settings_output_upper_limit = display_settings_get_output_upper_limit_for_overlay->call<float>(vm_context, display_settings);

    api->log_info("Backup display settings: sdr_gamma=%d, sdr_output_lower_limit=%d, sdr_output_upper_limit=%d, hdr_highlight=%d, hdr_shadow=%d, hdr_max_nits=%d, hdr_white_nits=%d, hdr_white_nits_for_overlay=%d, hdr_saturation=%d",
        sdr_gamma, sdr_output_lower_limit, sdr_output_upper_limit, hdr_highlight, hdr_shadow, hdr_max_nits, hdr_white_nits, hdr_white_nits_for_overlay, hdr_saturation);

    api->log_info("Backup display settings: use_sdr_brightness_option_for_overlay=%d, sdr_display_settings_ui_gamma=%f, sdr_display_settings_output_lower_limit=%f, sdr_display_settings_output_upper_limit=%f",
        use_sdr_brightness_option_for_overlay, sdr_display_settings_ui_gamma, sdr_display_settings_output_lower_limit, sdr_display_settings_output_upper_limit);
}

void GameUIController::restore_necessary_display_settings() {
    auto &api = reframework::API::get();
    auto vm_context = api->get_vm_context();

    if (gui_system_module_option == nullptr) {
        api->log_error("gui_system_module_option is null, can't restore brightness settings");
        return;
    }

    api->log_info("Restoring display settings: use_sdr_brightness_option_for_overlay=%d", use_sdr_brightness_option_for_overlay);

    if (use_sdr_brightness_option_for_overlay) {
        api->log_info("Custom brightness for UI is enabled by some mod, restoring display settings for overlay");
        display_settings_set_output_lower_limit_for_overlay->call<void>(vm_context, display_settings, sdr_display_settings_output_lower_limit);
        display_settings_set_output_upper_limit_for_overlay->call<void>(vm_context, display_settings, sdr_display_settings_output_upper_limit);
        display_settings_set_UseSDRBrightnessOptionForOverlay->call<void>(vm_context, display_settings, use_sdr_brightness_option_for_overlay);
        display_settings_set_gamma_for_overlay->call<void>(vm_context, display_settings, sdr_display_settings_ui_gamma);

        display_settings_update_request->call<void>(vm_context, display_settings);
    } else {
        api->log_info("Custom brightness for UI is not enabled, restoring display settings for master brightness");        

        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_SDR_GAMMA_OPTION_ID, sdr_gamma, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_SDR_LIMIT_LOWER_OPTION_ID, sdr_output_lower_limit, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_SDR_LIMIT_UPPER_OPTION_ID, sdr_output_upper_limit, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_HIGHLIGHT_OPTION_ID, hdr_highlight, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_SHADOW_OPTION_ID, hdr_shadow, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_MAX_NITS_OPTION_ID, hdr_max_nits, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_WHITE_NITS_OPTION_ID, hdr_white_nits, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_UI_WHITE_NITS_OPTION_ID, hdr_white_nits_for_overlay, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_SATURATION_OPTION_ID, hdr_saturation, 0);
    }
}

void GameUIController::apply_neutral_brightness_settings() {
    auto &api = reframework::API::get();
    auto vm_context = api->get_vm_context();

    if (gui_system_module_option == nullptr) {
        api->log_error("gui_system_module_option is null, can't apply neutral brightness settings");
        return;
    }

    if (use_sdr_brightness_option_for_overlay) {
        api->log_info("Custom brightness for UI is enabled by some mod, applying neutral brightness settings to it temporarily");

        display_settings_set_output_lower_limit_for_overlay->call<void>(vm_context, display_settings, NEUTRAL_SDR_DISPLAY_SETTINGS_OUTPUT_LOWER_LIMIT);
        display_settings_set_output_upper_limit_for_overlay->call<void>(vm_context, display_settings, NEUTRAL_SDR_DISPLAY_SETTINGS_OUTPUT_UPPER_LIMIT);
        display_settings_set_gamma_for_overlay->call<void>(vm_context, display_settings, NEUTRAL_SDR_DISPLAY_SETTINGS_UI_GAMMA);
        display_settings_update_request->call<void>(vm_context, display_settings);
    } else {
        api->log_info("Custom brightness for UI is not enabled, adjusting master brightness settings");

        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_SDR_GAMMA_OPTION_ID, NEUTRAL_BRIGHTNESS_SDR_GAMMA, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_SDR_LIMIT_LOWER_OPTION_ID, NEUTRAL_BRIGHTNESS_SDR_LIMIT_LOWER, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_SDR_LIMIT_UPPER_OPTION_ID, NEUTRAL_BRIGHTNESS_SDR_LIMIT_UPPER, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_HIGHLIGHT_OPTION_ID, NEUTRAL_BRIGHTNESS_HDR_HIGHLIGHT, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_SHADOW_OPTION_ID, NEUTRAL_BRIGHTNESS_HDR_SHADOW, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_MAX_NITS_OPTION_ID, NEUTRAL_BRIGHTNESS_HDR_MAX_NITS, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_WHITE_NITS_OPTION_ID, NEUTRAL_BRIGHTNESS_HDR_WHITE_NITS, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_UI_WHITE_NITS_OPTION_ID, NEUTRAL_BRIGHTNESS_HDR_WHITE_NITS_FOR_OVERLAY, 0);
        gui_system_module_option_apply_setting->call<void>(vm_context, gui_system_module_option, BRIGHTNESS_HDR_SATURATION_OPTION_ID, NEUTRAL_BRIGHTNESS_HDR_SATURATION, 0);
    }
}

void GameUIController::set_is_in_quest_result(bool should) {
    if (is_in_quest_result == should) {
        return;
    }

    is_in_quest_result = should;

    auto mod_settings = ModSettings::get_instance();

    if (mod_settings == nullptr) {
        return;
    }

    auto &api = reframework::API::get();

    if (!mod_settings->auto_fix_quest_result_brightness) {
        api->log_info("Auto fix quest result brightness is disabled, not applying settings");
        return;
    }

    if (is_in_quest_result) {
        api->log_info("In quest result, applying neutral brightness settings");

        backup_necessary_display_settings();
        apply_neutral_brightness_settings();
    } else {
        api->log_info("Exit quest result, restoring previous brightness settings");

        restore_necessary_display_settings();
    }
}

void GameUIController::end_rendering() {
    if (is_in_hide()) {
        if (!is_hiding_until_notice()) {
            hide_frames_passed++;
        }
    }
}