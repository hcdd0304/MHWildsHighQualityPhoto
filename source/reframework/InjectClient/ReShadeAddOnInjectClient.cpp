#include "ReShadeAddOnInjectClient.hpp"
#include "../MHWildsTypes.h"
#include "../../reshade/Plugin.h"
#include "../ModSettings.hpp"
#include "../GameUIController.hpp"
#include "../CaptureResolutionInject.hpp"

#include <reframework/API.hpp>
#include <webp/encode.h>

#include <BS_thread_pool.hpp>

#include <avir.h>
#include <avir_float4_sse.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <future>
#include <fstream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include "../REFrameworkBorrowedAPI.hpp"

class avir_scale_thread_pool : public avir::CImageResizerThreadPool
{
private:
    BS::thread_pool<> thread_pool;
public:
    virtual int getSuggestedWorkloadCount() const override
    {
        return static_cast<int>(thread_pool.get_thread_count());
    }

    virtual void addWorkload(CWorkload *const workload) override
    {
        _workloads.emplace_back(workload);
    }

    virtual void startAllWorkloads() override
    {
        for (auto &workload : _workloads) _tasks.emplace_back(thread_pool.submit_task([workload](){ workload->process(); }));
    }

    virtual void waitAllWorkloadsToFinish() override
    {
        for (auto &task : _tasks) task.wait();
    }

    virtual void removeAllWorkloads()
    {
        _tasks.clear();
        _workloads.clear();
    }

private:
    std::deque<std::future<void>> _tasks;
    std::deque<CWorkload*> _workloads;
};

namespace {
    // Tolerances used when detecting black bars (letterboxing/pillarboxing) in a captured
    // frame. This happens when the game renders a wider aspect ratio (eg 21:9) than the
    // monitor supports (eg 16:9), drawing the actual content in the center with black bars.
    constexpr int BLACK_BAR_PIXEL_THRESHOLD = 24;             // RGB channels <= this count as black
    constexpr float BLACK_BAR_CONTENT_MIN_FRACTION = 0.005f;   // min fraction of a line that must be non-black to count as content
    constexpr float BLACK_BAR_ASPECT_TOLERANCE = 0.05f;        // allowed aspect-ratio difference between cropped content and target

    struct BlackBarCropRect {
        int left = 0;
        int top = 0;
        int right = 0;   // exclusive
        int bottom = 0;  // exclusive
    };

    // Detects the bounding box of the actual rendered content inside `data` (RGBA, 4 bytes/px),
    // ignoring black bars. Returns false when there's nothing meaningful to crop.
    bool detect_black_bar_crop(const std::uint8_t* data, int width, int height, BlackBarCropRect& out) {
        const int stride = width * 4;
        const int content_count_row = std::max(1, static_cast<int>(width * BLACK_BAR_CONTENT_MIN_FRACTION));
        const int content_count_col = std::max(1, static_cast<int>(height * BLACK_BAR_CONTENT_MIN_FRACTION));

        std::vector<bool> row_has_content(static_cast<std::size_t>(height), false);
        for (int y = 0; y < height; ++y) {
            const std::uint8_t* row = data + static_cast<std::size_t>(y) * stride;
            int content_pixels = 0;
            for (int x = 0; x < width; ++x) {
                const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
                if (p[0] > BLACK_BAR_PIXEL_THRESHOLD || p[1] > BLACK_BAR_PIXEL_THRESHOLD || p[2] > BLACK_BAR_PIXEL_THRESHOLD) {
                    if (++content_pixels >= content_count_row) {
                        break;
                    }
                }
            }
            row_has_content[static_cast<std::size_t>(y)] = (content_pixels >= content_count_row);
        }

        int top = 0;
        while (top < height && !row_has_content[static_cast<std::size_t>(top)]) {
            ++top;
        }

        int bottom = height;
        while (bottom > top && !row_has_content[static_cast<std::size_t>(bottom - 1)]) {
            --bottom;
        }

        int left = 0;
        int right = width;

        if (bottom > top) {
            std::vector<bool> col_has_content(static_cast<std::size_t>(width), false);
            for (int x = 0; x < width; ++x) {
                int content_pixels = 0;
                for (int y = top; y < bottom; ++y) {
                    const std::uint8_t* p = data + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
                    if (p[0] > BLACK_BAR_PIXEL_THRESHOLD || p[1] > BLACK_BAR_PIXEL_THRESHOLD || p[2] > BLACK_BAR_PIXEL_THRESHOLD) {
                        if (++content_pixels >= content_count_col) {
                            break;
                        }
                    }
                }
                col_has_content[static_cast<std::size_t>(x)] = (content_pixels >= content_count_col);
            }

            while (left < width && !col_has_content[static_cast<std::size_t>(left)]) {
                ++left;
            }
            while (right > left && !col_has_content[static_cast<std::size_t>(right - 1)]) {
                --right;
            }
        }

        const int crop_width = right - left;
        const int crop_height = bottom - top;

        if (crop_width <= 0 || crop_height <= 0) {
            return false;
        }

        // Only crop when the removed bars are meaningful (>= ~1% of the smaller dimension),
        // so detection noise can't eat into actual content.
        const int min_bar = std::max(4, std::min(width, height) / 100);
        const bool has_bars = left >= min_bar || (width - right) >= min_bar ||
                              top >= min_bar || (height - bottom) >= min_bar;

        if (!has_bars) {
            return false;
        }

        out = { left, top, right, bottom };
        return true;
    }

    // Whether cropping `rect` keeps an aspect ratio close enough to
    // `target_width`x`target_height` that resizing won't visibly distort the content.
    bool crop_aspect_compatible(const BlackBarCropRect& rect, int target_width, int target_height) {
        const int crop_width = rect.right - rect.left;
        const int crop_height = rect.bottom - rect.top;

        if (crop_width <= 0 || crop_height <= 0 || target_width <= 0 || target_height <= 0) {
            return false;
        }

        const float crop_aspect = static_cast<float>(crop_width) / static_cast<float>(crop_height);
        const float target_aspect = static_cast<float>(target_width) / static_cast<float>(target_height);

        return std::abs(crop_aspect - target_aspect) <= BLACK_BAR_ASPECT_TOLERANCE * target_aspect;
    }
}

std::unique_ptr<ReShadeAddOnInjectClient> reshade_addon_client_instance = nullptr;
std::unique_ptr<avir_scale_thread_pool> avir_thread_pool_instance = nullptr;

BS::thread_pool<> random_task_thread_pool(4);

static const char *RESHADE_ADDON_NAME = "MHWildsHighQualityPhoto_Reshade.addon";
//static const char *END_SLOWMO_PLUGIN_NAME = "end_slowmo.dll";
static const char *GET_SCREEN_CAPTURE_SYMBOL_NAME = "request_screen_capture";
static const char *SET_RESHADE_FILTERS_ENABLE = "set_reshade_filters_enable";

const float QUALITY_REDUCE_STEP = 10.0f;
const float MIN_QUALITY_PHOTO = 10.0f;

const int HIDE_UI_FRAMES_COUNT_MIN = 6;
const float START_CAPTURE_AFTER_HIDE_REACHED_PROGRESS = 0.5f;

// NOTE: Change depends on monitor if needed
const int FORCE_SIZE_WIDTH_16x9 = 1920;
const int FORCE_SIZE_HEIGHT_16x9 = 1080;

const int FORCE_SIZE_WIDTH_21x9 = 2560;
const int FORCE_SIZE_HEIGHT_21x9 = 1080;

ReShadeAddOnInjectClient* ReShadeAddOnInjectClient::get_instance() {
    return reshade_addon_client_instance ? reshade_addon_client_instance.get() : nullptr;
}

void ReShadeAddOnInjectClient::initialize() {
    if (reshade_addon_client_instance == nullptr) {
        reshade_addon_client_instance = std::unique_ptr<ReShadeAddOnInjectClient>(new ReShadeAddOnInjectClient());
    }

    if (avir_thread_pool_instance == nullptr) {
        avir_thread_pool_instance = std::make_unique<avir_scale_thread_pool>();
    }
}

/*
bool ReShadeAddOnInjectClient::end_slowmo_present() {
    if (!slowmo_present_cached) {
        slowmo_present_cached = true;
        slowmo_present = false;

        auto &api = reframework::API::get();

        HMODULE slowmo_module = nullptr;
        auto success = GetModuleHandleExA(0, END_SLOWMO_PLUGIN_NAME, &slowmo_module);

        if (success && slowmo_module != nullptr) {
            slowmo_present = true;
            api->log_info("Found end slowmo plugin");

            FreeLibrary(slowmo_module);
        } else {
            api->log_info("End slowmo plugin not found, quest result image may not be correct");
        }
    }
    return false;
}*/

bool ReShadeAddOnInjectClient::provide_webp_data(bool is16x9, ProvideFinishedDataCallback provide_data_finish_callback) {
    if (!is_enabled) {
        reframework::API::get()->log_info("ReShadeAddOnInjectClient is not enabled.");
        return false;
    }

    if (!is_requested) {
        return false;
    }

    if (provide_data_finish_callback == nullptr) {
        return false;
    }

    is_requested = false;
    done_capture = false;

    auto game_ui_controller = GameUIController::get_instance();
    if (game_ui_controller == nullptr) {
        reframework::API::get()->log_info("GameUIController instance is null.");
        return false;
    }

    this->provide_data_finish_callback = provide_data_finish_callback;
    this->is_16x9 = is16x9;
    this->request_launched = false;

    do_prepare_capture();

    return true;
}

void ReShadeAddOnInjectClient::do_prepare_capture() {
    auto mod_settings = ModSettings::get_instance();
    auto game_ui_controller = GameUIController::get_instance();

    this->prepare_state = CapturePrepareState::FreezeScene;

    freeze_timescale_frame_total = std::max<int>(MIN_FREEZE_TIMESCALE_FRAME_COUNT, mod_settings->freeze_game_frames);
    freeze_timescale_frame_left = freeze_timescale_frame_total;
    should_skip_camera_update = true;
    hunter_set_mot_group_stance_params_cache.clear();

    game_ui_controller->hide_for(freeze_timescale_frame_total);
}

int ReShadeAddOnInjectClient::pre_player_camera_controller_update_action(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    auto game_ui_controller = GameUIController::get_instance();

    if (reshade_addon_client_instance->should_skip_camera_update) {
        return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
    }

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void ReShadeAddOnInjectClient::post_player_camera_controller_update_action(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {

}

void ReShadeAddOnInjectClient::update() {
    if (!is_enabled) {
        return;
    }

    auto game_ui_controller = GameUIController::get_instance();
    auto mod_settings = ModSettings::get_instance();
    auto &api = reframework::API::get();

    if (game_ui_controller == nullptr || mod_settings == nullptr) {
        return;
    }

    if (!request_launched) {
        if (prepare_state == CapturePrepareState::FreezeScene) {
            auto frame_freezed = freeze_timescale_frame_total - freeze_timescale_frame_left;

            if (frame_freezed >= freeze_timescale_frame_total - 1) {
                prepare_state = CapturePrepareState::WaitingHideUI;

#if LOG_DEBUG_STEP
                api->log_info("Freeze game complete, move to start screenshotting");
#endif
            }
            else {
                api->log_info("Still waiting for timescale freeze");
            }
        }

        if (prepare_state == CapturePrepareState::WaitingHideUI) {
            float hide_progress = game_ui_controller->get_hiding_progress();

            if (hide_progress >= START_CAPTURE_AFTER_HIDE_REACHED_PROGRESS) {
                prepare_state = CapturePrepareState::Complete;
            }
        }

        if (prepare_state == CapturePrepareState::Complete) {
#if LOG_DEBUG_STEP
            api->log_info("Starting screenshot process");
#endif
            // Start the capture process
            launch_capture_implement();
            prepare_state = CapturePrepareState::None;
        }
    }
}

void ReShadeAddOnInjectClient::late_update() {
    if (!is_enabled) {
        return;
    }

    auto& api = reframework::API::get();

    if (api == nullptr) {
        return;
    }

    auto vm_context = api->get_vm_context();

    if (freeze_timescale_frame_left >= 0) {
        if (!time_scale_cached) {
            previous_timescale = get_timescale_method->call<float>(vm_context);

            time_scale_cached = true;
        }

        // NOTE: Setting timescale completely to 0 will mess up some VFX (black)
        // Let it have some leeway
        float target_timescale = 0.000001f;

        if (freeze_timescale_frame_left == 0) {
            target_timescale = previous_timescale;
            execute_pending_mot_group_stance();
            time_scale_cached = false;
        }

        api->log_info("Freezing timescale, frame left: %d, total: %d, target frame scale: %f", freeze_timescale_frame_left, freeze_timescale_frame_total, target_timescale);
        set_timescale_method->call<void>(vm_context, target_timescale);
    }
}

void ReShadeAddOnInjectClient::end_rendering() {
    if (!is_enabled) {
        return;
    }

    if (freeze_timescale_frame_left >= 0) {
        freeze_timescale_frame_left--;

        auto& api = reframework::API::get();

        api->log_info("End rendering, timescale freeze frame left: %d", freeze_timescale_frame_left);
    }
}

void ReShadeAddOnInjectClient::launch_capture_implement() {
    auto& api = reframework::API::get();
    auto game_ui_controller = GameUIController::get_instance();
    auto mod_settings = ModSettings::get_instance();

    if (game_ui_controller == nullptr || mod_settings == nullptr) {
        return;
    }

    if (is_reshade_present()) {
        bool screenshot_before_reshade = quest_result_hq_background_mode == QuestResultHQBackgroundMode::NoReshade ||
            quest_result_hq_background_mode == QuestResultHQBackgroundMode::ReshadeApplyLater;

        auto request_capture = request_reshade_screen_capture(capture_screenshot_callback, mod_settings->hdr_bits, screenshot_before_reshade);
        if (request_capture != RESULT_SCREEN_CAPTURE_SUBMITTED) {
            api->log_error("Request capture failed %d", request_capture);
            finish_capture(false);
        }
        else {
            api->log_info("Request capture submitted successfully");
        }
    } else {
        api->log_error("Failed to load reshade module");
        finish_capture(false);
    }

    request_launched = true;
}

bool ReShadeAddOnInjectClient::try_load_reshade() {
    if (reshade_module != nullptr) {
        return true;
    }

    auto& api = reframework::API::get();

    if (reshade_module == nullptr) {
        bool success = GetModuleHandleExA(0, RESHADE_ADDON_NAME, &reshade_module);
        if (!success || reshade_module == nullptr) {
            api->log_error("Can't detect ReShade plugin, please make sure you have ReShade with Add-On Support installed", RESHADE_ADDON_NAME);
            return false;
        }
    }

    if (request_reshade_screen_capture == nullptr) {
        request_reshade_screen_capture = reinterpret_cast<request_screen_capture_func>(GetProcAddress(reshade_module, GET_SCREEN_CAPTURE_SYMBOL_NAME));
    }

    if (set_reshade_filters_enable == nullptr) {
        set_reshade_filters_enable = reinterpret_cast<set_reshade_filters_enable_func>(GetProcAddress(reshade_module, SET_RESHADE_FILTERS_ENABLE));
    }

    return request_reshade_screen_capture != nullptr;
}


void ReShadeAddOnInjectClient::compress_webp_thread(std::uint8_t *data, int width, int height) {
    auto& api = reframework::API::get();

    if (data == nullptr) {
        api->log_info("Data is null");
        reshade_addon_client_instance->finish_capture(false);

        return;
    }

#ifdef LOG_DEBUG_STEP
    api->log_info("Compressing image data to WebP format");
#endif

    auto mod_settings = ModSettings::get_instance();

    std::vector<std::uint8_t> new_buffer_if_have;
    std::vector<std::uint8_t> cropped_buffer_if_have;

    int force_size_width = FORCE_SIZE_WIDTH_16x9;
    int force_size_height = FORCE_SIZE_HEIGHT_16x9;

    auto capture_resolution_inject = CaptureResolutionInject::get_instance();

    if (!reshade_addon_client_instance->is_photo_mode) {
        if (reshade_addon_client_instance->is_16x9) {
            auto resolution = capture_resolution_inject->get_current_resolution_16x9();
            force_size_width = resolution.first;
            force_size_height = resolution.second;
        } else {
            auto resolution = capture_resolution_inject->get_current_resolution_21x9();
            force_size_width = resolution.first;
            force_size_height = resolution.second;
        }
    }

    // When the game renders a wider aspect ratio than the monitor supports (eg 21:9
    // letterboxed on a 16:9 screen), the captured frame contains black bars on the
    // top/bottom (and/or left/right). Crop those bars out before resizing so the actual
    // rendered content is kept intact and isn't stretched or keeps the black bars.
    if (mod_settings != nullptr && mod_settings->crop_black_bars) {
        BlackBarCropRect crop_rect;
        if (detect_black_bar_crop(data, width, height, crop_rect) &&
            crop_aspect_compatible(crop_rect, force_size_width, force_size_height)) {
            const int crop_width = crop_rect.right - crop_rect.left;
            const int crop_height = crop_rect.bottom - crop_rect.top;

            api->log_info("Cropping black bars from %dx%d: left %d, top %d, right %d, bottom %d (content %dx%d)",
                width, height, crop_rect.left, crop_rect.top, crop_rect.right, crop_rect.bottom, crop_width, crop_height);

            // Letterboxing (bars only on top/bottom) keeps the content spanning the full width,
            // so the cropped rows are still contiguous with the same stride. Just advance the
            // pointer to the first content row and shrink the height - no copy needed.
            if (crop_rect.left == 0 && crop_rect.right == width) {
                data += static_cast<std::size_t>(crop_rect.top) * width * 4;
                width = crop_width;   // unchanged
                height = crop_height;
            } else {
                // Pillarboxing (bars on left/right): rows are not contiguous, copy into a tight buffer.
                cropped_buffer_if_have.resize(static_cast<std::size_t>(crop_width) * crop_height * 4);

                for (int y = 0; y < crop_height; ++y) {
                    std::memcpy(
                        cropped_buffer_if_have.data() + static_cast<std::size_t>(y) * crop_width * 4,
                        data + static_cast<std::size_t>(crop_rect.top + y) * width * 4 + crop_rect.left * 4,
                        static_cast<std::size_t>(crop_width) * 4);
                }

                data = cropped_buffer_if_have.data();
                width = crop_width;
                height = crop_height;
            }
        }
    }

    if (force_size_width != width || force_size_height != height) {
        // Need resize
        api->log_info("Resizing image from %dx%d to %dx%d (GAME REQUIRES IT)", width, height, force_size_width, force_size_height);

        new_buffer_if_have.resize(force_size_width * force_size_height * 4);
        avir::CImageResizer<avir::fpclass_float4> image_resizer( 8 );

        avir::CImageResizerVars params;
        std::memset(&params, 0, sizeof(params));

        params.ThreadPool = avir_thread_pool_instance.get();

        image_resizer.resizeImage(data, width, height, 0, new_buffer_if_have.data(), force_size_width,
            force_size_height, 4, 0, &params);

        data = new_buffer_if_have.data();
        width = force_size_width;
        height = force_size_height;
    }

    // Set alpha all to 1, for some reasons alpha on some machine is not 1
    for (int i = 0; i < width * height; i++) {
        std::uint8_t alpha = data[i * 4 + 3];
        data[i * 4 + 3] = 255;
    }
    
    bool is_lossless = reshade_addon_client_instance->is_lossless();

    std::uint8_t* result_temp = nullptr;
    std::size_t result_size = 0;

    std::size_t max_size = reshade_addon_client_instance->use_old_limit_size ? MaxSerializePhotoSizeOriginal : MaxSerializePhotoSize;
    
    if (is_lossless) {
        result_size = WebPEncodeLosslessRGBA(data, width, height, width * 4, &result_temp);
    } else {
        float min_quality = MIN_QUALITY_PHOTO;
        float current_quality = std::max<float>(MIN_QUALITY_PHOTO, static_cast<float>(ModSettings::get_instance()->max_album_image_quality));

        do {
            result_size = WebPEncodeRGBA(data, width, height, width * 4, current_quality, &result_temp);
            if (result_size == 0) {
                api->log_info("Failed to encode image data to WebP format.");
                break;
            }

            if (result_size >= max_size) {
                current_quality -= QUALITY_REDUCE_STEP;
                api->log_info("Image size too large, reducing quality to %f", current_quality);

                result_size = 0;

                WebPFree(result_temp);
                result_temp = nullptr;
            } else {
                break;
            }
        } while (current_quality >= min_quality);
    }

    if (mod_settings->debug_capture_delay) {
        api->log_info("Debug capture delay enabled, simulating delay of %f seconds", mod_settings->simulate_capture_delay_seconds);
        std::this_thread::sleep_for(std::chrono::duration<float>(mod_settings->simulate_capture_delay_seconds));
    }

    if (result_size > 0) {
        std::vector<std::uint8_t> temp_buffer(result_temp, result_temp + result_size);
        api->log_info("Screenshot image encoded successfully, size: %zu bytes", result_size);

#if 0
        std::ofstream test_result("E:\\test_result.webp");
        test_result.write(reinterpret_cast<const char*>(temp_buffer.data()), temp_buffer.size());
        test_result.flush();
        test_result.close();
#endif
        reshade_addon_client_instance->finish_capture(true, &temp_buffer);

        WebPFree(result_temp);
    } else {
        // Handle error
        api->log_info("Failed to encode image data to WebP format.");
        reshade_addon_client_instance->finish_capture(false);
    }

    reshade_addon_client_instance->done_capture = true;
}

void ReShadeAddOnInjectClient::capture_screenshot_callback(int result, int width, int height, void* data) {
    auto& api = reframework::API::get();
    auto mod_settings = ModSettings::get_instance();

    if (result == RESULT_SCREEN_CAPTURE_DATA_DOWNLOADED) {
        api->log_info("Frame data downloaded, continuing camera");

        // Done with the screenshot, restore back the camera request
        reshade_addon_client_instance->restore_back_hunt_complete_camera_request();
        reshade_addon_client_instance->should_skip_camera_update = false;

        return;
    }

#ifdef LOG_DEBUG_STEP
    api->log_info("Capture screenshot callback called with result: %d, width: %d, height: %d", result, width, height);
#endif

    if (result == RESULT_SCREEN_CAPTURE_SUCCESS) {
        if (data == nullptr) {
            api->log_info("ReShade's screenshot data is null");
            reshade_addon_client_instance->finish_capture(false);

            return;
        }

        if (reshade_addon_client_instance->dump_promise.valid()) {
            reshade_addon_client_instance->dump_promise.wait();
        }

        if (reshade_addon_client_instance->webp_promise.valid()) {
            reshade_addon_client_instance->webp_promise.wait();
        }

        auto &data_cache = reshade_addon_client_instance->screenshot_data_cache;
        auto size_buffer_needed = static_cast<std::size_t>(width * height * 4);

        if (data_cache.size() < size_buffer_needed) {
            data_cache.resize(size_buffer_needed);
        }

        std::memcpy(data_cache.data(), data, size_buffer_needed);

        auto data_ptr = data_cache.data();

#ifdef LOG_DEBUG_STEP
        api->log_info("Calling WebP compress thread");
#endif

        reshade_addon_client_instance->dump_promise = random_task_thread_pool.submit_task([data_ptr, width, height, dump_debug_png = mod_settings->dump_mod_png]() {
            if (dump_debug_png) {
                auto persistent_dir = REFramework::get_persistent_dir();

                static constexpr const char *DEBUG_FILE_NAME= "reframework/data/MHWilds_HighQualityPhotoMod_HighQuality_QuestResult.png";

                if (!std::filesystem::exists(persistent_dir)) {
                    std::filesystem::create_directories(persistent_dir);
                }

                auto debug_path = persistent_dir / DEBUG_FILE_NAME;
                auto debug_path_str = debug_path.string();

                // Dump the actual cropped content (without black bars) when the crop setting
                // is enabled, so the debug image matches the content that gets resized/encoded.
                const std::uint8_t* dump_data = data_ptr;
                int dump_width = width;
                int dump_height = height;
                std::vector<std::uint8_t> cropped_dump_buffer;

                auto mod_settings = ModSettings::get_instance();
                if (mod_settings != nullptr && mod_settings->crop_black_bars) {
                    BlackBarCropRect crop_rect;
                    if (detect_black_bar_crop(data_ptr, width, height, crop_rect)) {
                        const int crop_width = crop_rect.right - crop_rect.left;
                        const int crop_height = crop_rect.bottom - crop_rect.top;

                        // Full-width content (letterboxing): rows stay contiguous with the same
                        // stride, so just advance the pointer - no copy needed.
                        if (crop_rect.left == 0 && crop_rect.right == width) {
                            dump_data = data_ptr + static_cast<std::size_t>(crop_rect.top) * width * 4;
                            dump_width = crop_width;   // unchanged
                            dump_height = crop_height;
                        } else {
                            // Pillarboxing present: rows are not contiguous, copy into a tight buffer.
                            cropped_dump_buffer.resize(static_cast<std::size_t>(crop_width) * crop_height * 4);
                            for (int y = 0; y < crop_height; ++y) {
                                std::memcpy(
                                    cropped_dump_buffer.data() + static_cast<std::size_t>(y) * crop_width * 4,
                                    data_ptr + static_cast<std::size_t>(crop_rect.top + y) * width * 4 + crop_rect.left * 4,
                                    static_cast<std::size_t>(crop_width) * 4);
                            }
                            dump_data = cropped_dump_buffer.data();
                            dump_width = crop_width;
                            dump_height = crop_height;
                        }
                    }
                }

                stbi_write_png(debug_path_str.c_str(), dump_width, dump_height, 4, dump_data, dump_width * 4);
            }
        });

        reshade_addon_client_instance->webp_promise = random_task_thread_pool.submit_task([data_ptr, width, height, dump_debug_png = mod_settings->dump_mod_png]() {
            ReShadeAddOnInjectClient::compress_webp_thread(data_ptr, width, height);
        });
    } else {
        // Handle error
        api->log_info("Screen capture failed with error code: %d", result);
        reshade_addon_client_instance->finish_capture(false);
        reshade_addon_client_instance->done_capture = true;
    }
}

void ReShadeAddOnInjectClient::finish_capture(bool success, std::vector<std::uint8_t>* provided_data) {
    auto& api = reframework::API::get();

    if (success && provided_data) {
        provide_data_finish_callback(success, provided_data);
    } else {
        provide_data_finish_callback(false, nullptr);
    }
}

void ReShadeAddOnInjectClient::null_post(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    // No operation
}

bool ReShadeAddOnInjectClient::should_reshade_filters_disable_when_show_quest_result_ui() const {
    return quest_result_hq_background_mode == QuestResultHQBackgroundMode::NoReshade ||
           quest_result_hq_background_mode == QuestResultHQBackgroundMode::ReshadePreapplied;
}

int ReShadeAddOnInjectClient::pre_open_quest_result_ui(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (!reshade_addon_client_instance->is_enabled) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    if (!reshade_addon_client_instance->should_reshade_filters_disable_when_show_quest_result_ui()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    reshade_addon_client_instance->set_reshade_filters_enable(false);

    auto &api = reframework::API::get();
    if (api) {
        api->log_info("Quest result UI opened, disabling ReShade filters to show quest result background.");
    }

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int ReShadeAddOnInjectClient::pre_quest_success_free_playtime_on_enter_state(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (!is_enabled) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    auto &api = reframework::API::get();
    auto vm_context = api->get_vm_context();

    if (camera_manager_singleton == nullptr) {
        camera_manager_singleton = api->get_managed_singleton("app.CameraManager");
    }

    if (camera_manager_singleton == nullptr) {
        api->log_error("Can't find CameraManager singleton!");
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    auto player_camera_request_obj_ptr = camera_manager_singleton->get_field<reframework::API::ManagedObject*>("_PlCameraRequest");

    if (player_camera_request_obj_ptr == nullptr) {
        api->log_error("Can't find CameraManager's player camera request!");
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    player_camera_request_obj = *player_camera_request_obj_ptr;

    if (player_camera_request_obj == nullptr) {
        api->log_error("Player camera request object is null!");
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    // Backup current request flags
    auto flags_ptr = player_camera_request_obj->get_field<std::uint64_t>("_Flags");
    if (flags_ptr == nullptr) {
        api->log_error("Can't find Player camera request object's flags!");
    
        // Rollback
        player_camera_request_obj = nullptr;
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    player_camera_global_request_flags_backup = *flags_ptr;

    auto hunt_complete_target_obj_ptr = player_camera_request_obj->get_field<reframework::API::ManagedObject*>("_HuntComplete");
    if (hunt_complete_target_obj_ptr == nullptr) {
        api->log_error("Can't find Player camera request object's hunt complete target!");
    
        // Rollback
        player_camera_request_obj = nullptr;
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    hunt_complete_target_access_key_ptr_backup = *hunt_complete_target_obj_ptr;

    // Log it out that we have successfully backed up
    api->log_info("Backed up Player camera request object's flags: 0x%llX, hunt complete target: 0x%llX. Preparing to disable hunt complete.",
        player_camera_global_request_flags_backup,
        reinterpret_cast<uintptr_t>(hunt_complete_target_access_key_ptr_backup));

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int ReShadeAddOnInjectClient::pre_quest_success_free_playtime_on_enter_state_proxy(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (!reshade_addon_client_instance->is_enabled) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    return reshade_addon_client_instance->pre_quest_success_free_playtime_on_enter_state(argc, argv, arg_tys, ret_addr);
}

void ReShadeAddOnInjectClient::post_quest_success_free_playtime_on_enter_state(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {    
    auto &api = reframework::API::get();

    if (player_camera_request_obj == nullptr) {
        api->log_error("Player camera request object is null during post quest success!");
        return;
    }

    // In this function, we want to roll back the camera request to the backed up state
    // But at the same time, back up the current state where it has been modified to focus the camera on the monster, so later when we done all our shenanigans, we can restore it back
    auto current_flags_ptr = player_camera_request_obj->get_field<std::uint64_t>("_Flags");
    if (current_flags_ptr == nullptr) {
        api->log_error("Can't find Player camera request object's flags during post quest success!");
        return;
    }

    auto current_flags = *current_flags_ptr;

    auto hunt_complete_target_obj_ptr = player_camera_request_obj->get_field<reframework::API::ManagedObject*>("_HuntComplete");
    if (hunt_complete_target_obj_ptr == nullptr) {
        api->log_error("Can't find Player camera request object's hunt complete target during post quest success!");
        return;
    }

    auto current_hunt_complete_target = *hunt_complete_target_obj_ptr;

    auto old_flags = player_camera_global_request_flags_backup;
    auto old_hunt_complete_target = hunt_complete_target_access_key_ptr_backup;

    // Restore to backed up state
    *current_flags_ptr = old_flags;
    *hunt_complete_target_obj_ptr = old_hunt_complete_target;

    // Back up the current state again
    player_camera_global_request_flags_backup = current_flags;
    hunt_complete_target_access_key_ptr_backup = current_hunt_complete_target;

    api->log_info("Roll back camera request to disable hunt complete. Flags: 0x%llX, hunt complete target: 0x%llX",
        *current_flags_ptr,
        reinterpret_cast<uintptr_t>(*hunt_complete_target_obj_ptr));

    // Log backup
    api->log_info("Backed up Player camera request object's flags: 0x%llX, hunt complete target: 0x%llX",
        current_flags,
        reinterpret_cast<uintptr_t>(current_hunt_complete_target));
}

void ReShadeAddOnInjectClient::post_quest_success_free_playtime_on_enter_state_proxy(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    if (!reshade_addon_client_instance->is_enabled) {
        return;
    }

    reshade_addon_client_instance->post_quest_success_free_playtime_on_enter_state(ret_val, ret_ty, ret_addr);
}

void ReShadeAddOnInjectClient::restore_back_hunt_complete_camera_request() {
    auto &api = reframework::API::get();

    if (player_camera_request_obj == nullptr) {
        api->log_error("Player camera request object is null during restore back!");
        return;
    }

    auto hunt_complete_target_obj_ptr = player_camera_request_obj->get_field<reframework::API::ManagedObject*>("_HuntComplete");
    if (hunt_complete_target_obj_ptr == nullptr) {
        api->log_error("Can't find Player camera request object's hunt complete target during restore back!");
        return;
    }

    *hunt_complete_target_obj_ptr = hunt_complete_target_access_key_ptr_backup;

    // Restore the flags too
    auto flags_ptr = player_camera_request_obj->get_field<std::uint64_t>("_Flags");
    if (flags_ptr == nullptr) {
        api->log_error("Can't find Player camera request object's flags during restore back!");
        return;
    }

    *flags_ptr = player_camera_global_request_flags_backup;

    api->log_info("Allowing hunt complete to play. Flags: 0x%llX, hunt complete target: 0x%llX",
        player_camera_global_request_flags_backup,
        reinterpret_cast<uintptr_t>(hunt_complete_target_access_key_ptr_backup));
}

int ReShadeAddOnInjectClient::pre_close_quest_result_ui(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (!reshade_addon_client_instance->is_enabled) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    if (!reshade_addon_client_instance->should_reshade_filters_disable_when_show_quest_result_ui()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    reshade_addon_client_instance->set_reshade_filters_enable(true);

    auto &api = reframework::API::get();
    if (api) {
        api->log_info("Quest result UI closed, enabling ReShade filters.");
    }

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int ReShadeAddOnInjectClient::pre_quest_result_load_quest_result_photograph(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (!reshade_addon_client_instance->is_enabled) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    auto &api = reframework::API::get();

    // Do dirty
    if (!reshade_addon_client_instance->done_capture) {
        api->log_info("Waiting for screenshot capture to complete before loading quest result photograph...");

        while (!reshade_addon_client_instance->done_capture) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        reshade_addon_client_instance->manual_update_save_capture_until_complete();

        api->log_info("Screenshot capture completed, proceeding to load quest result photograph.");
    }

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void ReShadeAddOnInjectClient::manual_update_save_capture_until_complete() {
    auto &api = reframework::API::get();

    if (!album_manager_instance) {
        album_manager_instance = api->get_managed_singleton("app.AlbumManager");
    }

    if (!album_manager_instance) {
        api->log_info("Album manager is null to manually update save capture");
        return;
    }

    const int MAX_TRY_COUNT = 1000;

    int try_count = MAX_TRY_COUNT;
    auto vm_context = api->get_vm_context();

    while (try_count-- > 0) {
        auto capture_state_ptr = album_manager_instance->get_field<int>("_SaveCaptureState");

        if (capture_state_ptr == nullptr) {
            api->log_info("Capture state is null to manually update save capture");
            return;
        }

        SaveCaptureState capture_state = static_cast<SaveCaptureState>(*capture_state_ptr);

        // We are finished
        if (capture_state == SAVECAPTURESTATE_IDLE || capture_state >= SAVECAPTURESTATE_WAIT_SAVE_CAPTURE) {
            api->log_info("Manual update save capture finished");
            return;
        } else {
            auto msg = "Manual update save capture in progress, current state: " + std::to_string(static_cast<int>(capture_state));
            api->log_info(msg.c_str());
        }

        update_save_capture_method->call<void>(vm_context, album_manager_instance);
    }

    api->log_info("Manual update save capture reached max try count, basically failed");
    return;
}

ReShadeAddOnInjectClient::ReShadeAddOnInjectClient() {
    auto &api = reframework::API::get();

    // This is not stable
    auto player_camera_controller_type = api->tdb()->find_type("app.PlayerCameraController");
    if (player_camera_controller_type == nullptr) {
        api->log_error("Can't find PlayerCameraController type!");
        return;
    }

    // Iterate all methods and hook method that starts with updateAction
    auto player_camera_controller_methods = player_camera_controller_type->get_methods();
    for (auto& method : player_camera_controller_methods) {
        auto method_name = std::string_view(method->get_name());

        if (method_name.starts_with("updateAction") || method_name.starts_with("<updateAction>")) {
            method->add_hook(pre_player_camera_controller_update_action, post_player_camera_controller_update_action, false);
        }
    }

    auto quest_result_start_method = api->tdb()->find_method("app.GUIFlowQuestResult.cContext", "onStartFlow");
    quest_result_start_method->add_hook(pre_open_quest_result_ui, null_post, false);

    auto quest_result_end_method = api->tdb()->find_method("app.GUIFlowQuestResult.cContext", "onEndFlow");
    quest_result_end_method->add_hook(pre_close_quest_result_ui, null_post, false);

    auto quest_success_free_playtime_on_enter_state_method = api->tdb()->find_method("app.cQuestSuccessFreePlayTime", "enter");
    quest_success_free_playtime_on_enter_state_method->add_hook(pre_quest_success_free_playtime_on_enter_state_proxy, post_quest_success_free_playtime_on_enter_state_proxy, false);

    // This is for quest failure
    auto hunt_repel_on_enter_state_method = api->tdb()->find_method("app.fsm_action.StEmCameraRepel", "doAction");
    hunt_repel_on_enter_state_method->add_hook(pre_quest_success_free_playtime_on_enter_state_proxy, post_quest_success_free_playtime_on_enter_state_proxy, false);

    auto load_quest_result_photograph_method = api->tdb()->find_method("app.AlbumManager", "loadQuestResultPhoto");
    load_quest_result_photograph_method->add_hook(pre_quest_result_load_quest_result_photograph, null_post, false);

    update_save_capture_method = api->tdb()->find_method("app.AlbumManager", "updateSaveCapture");

    if (!try_load_reshade()) {
        api->log_error("Failed to load ReShade module");
    }

    auto tdb = api->tdb();

    if (tdb == nullptr) {
        return;
    }

    // NOTE: There is an alternative way of setting the timescale, using via.Scene.set_TimeScale
    // Though, it a lot of time make the game stutter, I'm not sure why. So this is preferred for now
    auto application_type = tdb->find_type("via.Application");

    set_timescale_method = application_type->find_method("set_GlobalSpeed");
    get_timescale_method = application_type->find_method("get_GlobalSpeed");

    set_mot_group_stance_method = tdb->find_method("app.HunterCharacter.cMotionSupporter", "setHunterMotGroup_Stance");
    if (set_mot_group_stance_method == nullptr) {
        api->log_error("Can't find HunterCharacter.cMotionSupporter.setHunterMotGroup_Stance method!");
    }

    //set_mot_group_stance_method->add_hook(pre_motion_supporter_set_hunter_mot_group_stance_proxy, null_post, false);

    auto quest_failed_action_enter = tdb->find_method("app.PlayerCommonAction.cQuestFailed", "doEnter");
    if (quest_failed_action_enter == nullptr) {
        api->log_error("Can't find PlayerCommonAction.cQuestFailed.doEnter method!");
    } else {
        //quest_failed_action_enter->add_hook(pre_player_common_action_quest_failed_proxy, post_player_common_action_quest_failed_proxy, false);
    }

    prepare_state = CapturePrepareState::None;
    freeze_timescale_frame_left = -1;
    should_skip_camera_update = false;
    done_capture = true;
}

ReShadeAddOnInjectClient::~ReShadeAddOnInjectClient() {
    if (reshade_module != nullptr) {
        FreeLibrary(reshade_module);
        reshade_module = nullptr;
    }

    if (webp_promise.valid()) {
        webp_promise.wait();
    }

    if (dump_promise.valid()) {
        dump_promise.wait();
    }
}

thread_local reframework::API::ManagedObject* current_action_chara = nullptr;

int ReShadeAddOnInjectClient::pre_player_common_action_quest_failed_impl(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    auto action_ptr = reinterpret_cast<reframework::API::ManagedObject*>(argv[1]);
    if (action_ptr == nullptr) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    auto vm_context = reframework::API::get()->get_vm_context();
    auto chara = action_ptr->call<reframework::API::ManagedObject*>("get_Chara", vm_context, action_ptr);

    if (chara == nullptr) {
        auto &api = reframework::API::get();
        api->log_error("Failed to get chara from action in pre_player_common_action_quest_failed_impl");

        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    current_action_chara = chara;

    if (hunter_set_mot_group_stance_params_cache.contains(current_action_chara)) {
        hunter_set_mot_group_stance_params_cache.erase(current_action_chara);
    }

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void ReShadeAddOnInjectClient::post_player_common_action_quest_failed_impl(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    if (current_action_chara != nullptr) {
        current_action_chara = nullptr;
    }
}

int ReShadeAddOnInjectClient::pre_motion_supporter_set_hunter_mot_group_stance_impl(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (current_action_chara != nullptr) {
        auto vm_context = reframework::API::get()->get_vm_context();

        HunterSetMotGroupStanceParams params = {
            reinterpret_cast<std::uint64_t>(argv[1]),
            reinterpret_cast<std::uint64_t>(argv[2]),
            reinterpret_cast<std::uint64_t>(argv[3]),
            reinterpret_cast<std::uint64_t>(argv[4]),
            reinterpret_cast<std::uint64_t>(argv[5]),
        };

        if (!hunter_set_mot_group_stance_params_cache.contains(current_action_chara)) {
            std::vector<HunterSetMotGroupStanceParams> params_vec = { params };
            hunter_set_mot_group_stance_params_cache.emplace(current_action_chara, params_vec);
        } else {
            hunter_set_mot_group_stance_params_cache[current_action_chara].push_back(params);
        }

        auto &api = reframework::API::get();
        api->log_info("Caching setHunterMotGroup_Stance call for chara 0x%llX, total pending calls for this chara: %zu", reinterpret_cast<uintptr_t>(current_action_chara), hunter_set_mot_group_stance_params_cache[current_action_chara].size());

        return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
    }

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int ReShadeAddOnInjectClient::pre_motion_supporter_set_hunter_mot_group_stance_proxy(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (!reshade_addon_client_instance->get_is_enabled()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    return reshade_addon_client_instance->pre_motion_supporter_set_hunter_mot_group_stance_impl(argc, argv, arg_tys, ret_addr);
}

int ReShadeAddOnInjectClient::pre_player_common_action_quest_failed_proxy(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (!reshade_addon_client_instance->get_is_enabled()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    return reshade_addon_client_instance->pre_player_common_action_quest_failed_impl(argc, argv, arg_tys, ret_addr);
}

void ReShadeAddOnInjectClient::post_player_common_action_quest_failed_proxy(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    if (!reshade_addon_client_instance->get_is_enabled()) {
        return;
    }

    reshade_addon_client_instance->post_player_common_action_quest_failed_impl(ret_val, ret_ty, ret_addr);
}

void ReShadeAddOnInjectClient::execute_pending_mot_group_stance() {
    auto& api = reframework::API::get();
    auto vm_context = api->get_vm_context();

    api->log_info("Executing pending setHunterMotGroup_Stance calls, total charas with pending calls: %zu", hunter_set_mot_group_stance_params_cache.size());

    for (auto& [chara, params_vec] : hunter_set_mot_group_stance_params_cache) {
        api->log_info("Executing pending setHunterMotGroup_Stance for chara 0x%llX with %zu pending calls", reinterpret_cast<uintptr_t>(chara), params_vec.size());

        for (auto& params : params_vec) {
            set_mot_group_stance_method->call<void>(vm_context, params[0], params[1], params[2], params[3], params[4]);
        }
    }

    hunter_set_mot_group_stance_params_cache.clear();
}