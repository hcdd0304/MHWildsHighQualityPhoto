#pragma once

#define NOMINMAX

#include "../WebPCaptureInjectClient.hpp"
#include "../QuestResultHQBackgroundMode.hpp"
#include "../../reshade/Plugin.h"

#include <reframework/API.hpp>

#include <string>

#include <atomic>
#include <memory>
#include <thread>

#include <Windows.h>

class ReShadeAddOnInjectClient : public WebPCaptureInjectClient {
public:
    static constexpr int MIN_FREEZE_TIMESCALE_FRAME_COUNT = 4;
    static constexpr int MAX_FREEZE_TIMESCALE_FRAME_COUNT = 16;

private:
    enum class CapturePrepareState {
        None,
        WaitingHideUI,
        FreezeScene,
        Complete
    };

    ProvideFinishedDataCallback provide_data_finish_callback;

    HMODULE reshade_module = nullptr;

    typedef int (*request_screen_capture_func)(ScreenCaptureFinishFunc finish_callback, int hdr_bit_depths, bool screenshot_before_reshade);
    typedef void (*set_reshade_filters_enable_func)(bool should_enable);

    request_screen_capture_func request_reshade_screen_capture = nullptr;
    set_reshade_filters_enable_func set_reshade_filters_enable = nullptr;

    std::unique_ptr<std::thread> webp_compress_thread = nullptr;
    QuestResultHQBackgroundMode quest_result_hq_background_mode = QuestResultHQBackgroundMode::ReshadeApplyLater;

    bool is_enabled = true;
    bool should_lossless = false;
    bool use_old_limit_size = false;
    bool is_16x9 = false;
    bool is_photo_mode = false;

    bool request_launched = false;
    bool slowmo_present = false;
    bool slowmo_present_cached = false;
    bool is_requested = false;

    CapturePrepareState prepare_state = CapturePrepareState::None;
    int freeze_timescale_frame_left = 0;
    int freeze_timescale_frame_total = 0;

    reframework::API::Method *set_timescale_method = nullptr;
    reframework::API::Method *get_timescale_method = nullptr;
    float previous_timescale = 1.0f;
    bool time_scale_cached = false;

private:
    bool try_load_reshade();
    bool end_slowmo_present();
    void finish_capture(bool success, std::vector<std::uint8_t>* provided_data = nullptr);

    static void compress_webp_thread(std::uint8_t *data, int width, int height);
    static void capture_screenshot_callback(int result, int width, int height, void* data);

    static int pre_player_camera_controller_update_action(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_player_camera_controller_update_action(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_open_quest_result_ui(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static int pre_close_quest_result_ui(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void null_post(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    bool should_reshade_filters_disable_when_show_quest_result_ui() const;
    void launch_capture_implement();

public:
    ~ReShadeAddOnInjectClient();

public:
    explicit ReShadeAddOnInjectClient();

    bool provide_webp_data(bool is16x9, ProvideFinishedDataCallback provide_data_finish_callback) override;
    void update();
    void late_update();
    void end_rendering();

    void set_enable(bool enable) {
        is_enabled = enable;
    }

    bool get_is_enabled() const {
        return is_enabled;
    }

    void set_lossless(bool lossless) {
        should_lossless = lossless;
    }

    bool is_lossless() const {
        return should_lossless;
    }

    void set_is_photo_mode(bool force) {
        is_photo_mode = force;
    }

    bool get_is_photo_mode() const {
        return is_photo_mode;
    }

    void set_use_old_limit_size(bool use_old_limit) {
        use_old_limit_size = use_old_limit;
    }

    bool get_use_old_limit_size() const {
        return use_old_limit_size;
    }

    bool is_reshade_present() {
        return reshade_module != nullptr && request_reshade_screen_capture != nullptr;
    }

    void set_hq_background_mode(QuestResultHQBackgroundMode mode) {
        quest_result_hq_background_mode = mode;
    }

    void set_requested() {
        is_requested = true;
    }

    bool get_requested() const {
        return is_requested;
    }

    QuestResultHQBackgroundMode get_hq_background_mode() const {
        return quest_result_hq_background_mode;
    }

    static ReShadeAddOnInjectClient* get_instance();
    static void initialize();
};