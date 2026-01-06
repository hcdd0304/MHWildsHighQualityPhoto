#pragma once

#include <reframework/API.h>
#include <reframework/API.hpp>

class GameUIController {
private:
    static constexpr const int BRIGHTNESS_HDR_HIGHLIGHT_OPTION_ID = 256;
    static constexpr const int BRIGHTNESS_HDR_MAX_NITS_OPTION_ID = 253;
    static constexpr const int BRIGHTNESS_HDR_SATURATION_OPTION_ID = 257;
    static constexpr const int BRIGHTNESS_HDR_SHADOW_OPTION_ID = 255;
    static constexpr const int BRIGHTNESS_HDR_UI_WHITE_NITS_OPTION_ID = 258;
    static constexpr const int BRIGHTNESS_HDR_WHITE_NITS_OPTION_ID = 254;
    static constexpr const int BRIGHTNESS_SDR_GAMMA_OPTION_ID = 252;
    static constexpr const int BRIGHTNESS_SDR_LIMIT_LOWER_OPTION_ID = 250;
    static constexpr const int BRIGHTNESS_SDR_LIMIT_UPPER_OPTION_ID = 251;

    static constexpr const int NEUTRAL_BRIGHTNESS_SDR_GAMMA = 10;
    static constexpr const int NEUTRAL_BRIGHTNESS_SDR_LIMIT_LOWER = 0;
    static constexpr const int NEUTRAL_BRIGHTNESS_SDR_LIMIT_UPPER = 20;
    static constexpr const int NEUTRAL_BRIGHTNESS_HDR_HIGHLIGHT = 10;
    static constexpr const int NEUTRAL_BRIGHTNESS_HDR_SHADOW = 10;
    static constexpr const int NEUTRAL_BRIGHTNESS_HDR_MAX_NITS = 10;
    static constexpr const int NEUTRAL_BRIGHTNESS_HDR_WHITE_NITS = 10;
    static constexpr const int NEUTRAL_BRIGHTNESS_HDR_WHITE_NITS_FOR_OVERLAY = 10;
    static constexpr const int NEUTRAL_BRIGHTNESS_HDR_SATURATION = 5;

    static constexpr const float NEUTRAL_SDR_DISPLAY_SETTINGS_UI_GAMMA = 1.0f;
    static constexpr const float NEUTRAL_SDR_DISPLAY_SETTINGS_OUTPUT_LOWER_LIMIT = 0.0f;
    static constexpr const float NEUTRAL_SDR_DISPLAY_SETTINGS_OUTPUT_UPPER_LIMIT = 1.0f;

    int total_hide_frame{ -1 };
    int hide_frames_passed{ -1 };

    bool is_in_quest_result;

    reframework::API::ManagedObject *notification_GO;
    reframework::API::ManagedObject *display_settings;
    reframework::API::ManagedObject *gui_system_module_option;

    reframework::API::Method *display_settings_get_UseSDRBrightnessOptionForOverlay;
    reframework::API::Method *display_settings_set_UseSDRBrightnessOptionForOverlay;
    reframework::API::Method *display_settings_get_gamma_for_overlay;
    reframework::API::Method *display_settings_get_output_lower_limit_for_overlay;
    reframework::API::Method *display_settings_get_output_upper_limit_for_overlay;
    reframework::API::Method *display_settings_set_gamma_for_overlay;
    reframework::API::Method *display_settings_set_output_lower_limit_for_overlay;
    reframework::API::Method *display_settings_set_output_upper_limit_for_overlay;
    reframework::API::Method *display_settings_update_request;
    reframework::API::Method *app_option_util_is_hdr_enabled;
    reframework::API::Method *gui_system_module_option_on_update;
    reframework::API::Method *gui_system_module_option_apply_setting;
    reframework::API::Method *gui_system_module_option_get_value;

    int sdr_gamma { 10 };
    int sdr_output_lower_limit { 0 };
    int sdr_output_upper_limit { 10 };

    int hdr_highlight { 10 };
    int hdr_shadow { 10 };
    int hdr_max_nits { 10 };
    int hdr_white_nits { 10 };
    int hdr_white_nits_for_overlay { 10 };
    int hdr_saturation { 5 };

    float sdr_display_settings_ui_gamma { 1.0f };
    float sdr_display_settings_output_lower_limit { 0.0f };
    float sdr_display_settings_output_upper_limit { 1.0f };

    bool use_sdr_brightness_option_for_overlay { false };
    
    void backup_necessary_display_settings();
    void restore_necessary_display_settings();
    void apply_neutral_brightness_settings();

    explicit GameUIController(const REFrameworkPluginInitializeParam* initialize_params);

    void show_impl();

private:
    static int pre_gui000020_on_gui_awake(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_gui000020_on_gui_awake(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_gui_system_module_option_on_update(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_gui_system_module_option_on_update(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

public:
    ~GameUIController() = default;

    void hide_for(int frame_count);
    void update();
    void end_rendering();

    float get_hiding_progress() const;

    bool is_in_hide() const {
        return hide_frames_passed >= 0;
    }

    bool is_hiding_until_notice() const {
        return total_hide_frame < 0;
    }

    void set_is_in_quest_result(bool should);

    bool get_is_in_quest_result() {
        return is_in_quest_result;
    }

    reframework::API::ManagedObject *get_notification_gameobject() {
        return notification_GO;
    }

    static GameUIController *get_instance();
    static void initialize(const REFrameworkPluginInitializeParam *initialize_params);
};