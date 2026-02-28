#pragma once

#include "QuestResultHQBackgroundMode.hpp"
#include <string>

enum PhotoModeImageQuality {
    // The mod will not interfere
    PhotoModeImageQuality_DoNotModify = 0,

    // Still apply filters but image is low quality
    PhotoModeImageQuality_LowQualityApplyFilters = 1,

    // Still apply filters but image is high quality
    PhotoModeImageQuality_HighQualityApplyFilters = 2,
};

struct ModSettings {
    bool enable_override_album_image = false;

    std::string override_album_image_path;

    bool enable_override_quest_success = false;

    std::string override_quest_complete_background_path;

    bool enable_override_quest_failure = false;

    std::string override_quest_failure_background_path;

    bool enable_override_quest_cancel = false;

    std::string override_quest_headback_background_path;

    // Use lossless image for quest result. This will result in crisp quest result background, but will create a visible stutter
    // in case the lossless image size is heavy (eg 4K image). The reason for the stutter is in the decoding process, which is
    // synchronously perform (we cant blame them, the image they produced is usually very light and small-size)
    bool use_lossless_image_for_quest_result = true;

    // For album image, the image size must not exceed 1MB. The quality can be adjusted
    // so that the image size is less than 1MB. Normally the mod will try to do 100% lossy compression, then
    // go 10% lower when the image size does not fit the requirements, but by setting this to, for example 50%,
    // the mod will starts from doing 50% lossy, then 40%... This reduces the encoding time
    int max_album_image_quality = 100;

    // The HDR bits used for screen capture
    int hdr_bits = 11;

    /// The mod will no longer try to interfere to make the screen capture quality higher. Only override mod settings will be used.
    bool disable_high_quality_screen_capture = false;

    PhotoModeImageQuality photo_mode_image_quality = PhotoModeImageQuality_LowQualityApplyFilters;

    QuestResultHQBackgroundMode quest_result_hq_background_mode = ReshadeApplyLater;

    bool dump_original_webp = false;

    bool dump_mod_png = false;

    bool disable_mod = false;

    bool hide_chat_notification = true;

    int hide_ui_before_capture_frame_count = 6;

    std::string debug_file_postfix;

    // If the UI has lower brightness on HDR or your brightness is not set to balanced on SDR, the quest result will
    // look a bit darker. This option reverts the UI's brightness to balanced so that the quest result image is displayed
    // correctly
    bool auto_fix_quest_result_brightness = true;

    bool fix_framegen_artifacts = true;

    int freeze_game_frames = 5;

    bool debug_capture_delay = false;

    float simulate_capture_delay_seconds = 17.0f;

    bool heavy_debug_logging = false;

    bool data_changed(const ModSettings &clone) {
        return enable_override_album_image != clone.enable_override_album_image ||
            override_album_image_path != clone.override_album_image_path ||
            enable_override_quest_success != clone.enable_override_quest_success ||
            override_quest_complete_background_path != clone.override_quest_complete_background_path ||
            enable_override_quest_failure != clone.enable_override_quest_failure ||
            override_quest_failure_background_path != clone.override_quest_failure_background_path ||
            enable_override_quest_cancel != clone.enable_override_quest_cancel ||
            override_quest_headback_background_path != clone.override_quest_headback_background_path ||
            use_lossless_image_for_quest_result != clone.use_lossless_image_for_quest_result ||
            max_album_image_quality != clone.max_album_image_quality ||
            hdr_bits != clone.hdr_bits ||
            disable_high_quality_screen_capture != clone.disable_high_quality_screen_capture ||
            photo_mode_image_quality != clone.photo_mode_image_quality ||
            dump_original_webp != clone.dump_original_webp ||
            disable_mod != clone.disable_mod ||
            quest_result_hq_background_mode != clone.quest_result_hq_background_mode ||
            hide_ui_before_capture_frame_count != clone.hide_ui_before_capture_frame_count ||
            dump_mod_png != clone.dump_mod_png ||
            hide_chat_notification != clone.hide_chat_notification ||
            auto_fix_quest_result_brightness != clone.auto_fix_quest_result_brightness ||
            fix_framegen_artifacts != clone.fix_framegen_artifacts ||
            freeze_game_frames != clone.freeze_game_frames ||
            simulate_capture_delay_seconds != clone.simulate_capture_delay_seconds ||
            debug_capture_delay != clone.debug_capture_delay ||
            heavy_debug_logging != clone.heavy_debug_logging;
    }

    bool is_high_quality_photo_mode_enabled() const {
        return !is_disable_high_quality_screen_capture() && (photo_mode_image_quality == PhotoModeImageQuality_DoNotModify);
    }

    bool is_disable_high_quality_screen_capture() const {
#ifdef ENABLE_HIGH_QUALITY_PHOTO_MODE
        return disable_high_quality_screen_capture;
#else
        return false;
#endif
    }

    bool get_use_lossless_image_for_quest_result() const {
        return true;
    }

    PhotoModeImageQuality get_photo_mode_image_quality() const {
#ifdef ENABLE_HIGH_QUALITY_PHOTO_MODE
        return photo_mode_image_quality;
#else
        return PhotoModeImageQuality_DoNotModify;
#endif
    }

    static void initialize(std::string_view settings_name);
    static ModSettings* get_instance();

    void load();
    void save();
};