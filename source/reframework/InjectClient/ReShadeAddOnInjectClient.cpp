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

std::unique_ptr<ReShadeAddOnInjectClient> reshade_addon_client_instance = nullptr;
std::unique_ptr<avir_scale_thread_pool> avir_thread_pool_instance = nullptr;

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

    std::vector<std::uint8_t> new_buffer_if_have;
    bool need_delete_data = true;

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

        need_delete_data = false;
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

    if (need_delete_data) {
        delete[] data;
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
}

void ReShadeAddOnInjectClient::capture_screenshot_callback(int result, int width, int height, void* data) {
    auto& api = reframework::API::get();
    auto mod_settings = ModSettings::get_instance();

#ifdef LOG_DEBUG_STEP
    api->log_info("Capture screenshot callback called with result: %d, width: %d, height: %d", result, width, height);
#endif

    if (result == RESULT_SCREEN_CAPTURE_SUCCESS) {
        if (data == nullptr) {
            api->log_info("ReShade's screenshot data is null");
            reshade_addon_client_instance->finish_capture(false);

            return;
        }

        std::uint8_t* data_copy = new std::uint8_t[width * height * 4];
        std::memcpy(data_copy, data, width * height * 4);

        // Clear alpha channels
        for (int i = 0; i < width * height; ++i) {
            data_copy[i * 4 + 3] = 0xFF; // Set alpha channel to 255 (opaque)
        }

        if (reshade_addon_client_instance->webp_compress_thread != nullptr) {
            reshade_addon_client_instance->webp_compress_thread->join();
            reshade_addon_client_instance->webp_compress_thread.reset();
        }

        if (mod_settings->dump_mod_png) {
            auto persistent_dir = REFramework::get_persistent_dir();

            static constexpr const char *DEBUG_FILE_NAME= "reframework/data/MHWilds_HighQualityPhotoMod_HighQuality_QuestResult.png";

            if (!std::filesystem::exists(persistent_dir)) {
                std::filesystem::create_directories(persistent_dir);
            }

            auto debug_path = persistent_dir / DEBUG_FILE_NAME;
            auto debug_path_str = debug_path.string();

            stbi_write_png(debug_path_str.c_str(), width, height, 4, data_copy, width * 4);
        }

#ifdef LOG_DEBUG_STEP
        api->log_info("Calling WebP compress thread");
#endif

        reshade_addon_client_instance->webp_compress_thread = std::make_unique<std::thread>(compress_webp_thread, data_copy, width, height);
    } else {
        // Handle error
        api->log_info("Screen capture failed with error code: %d", result);
        reshade_addon_client_instance->finish_capture(false);
    }

    // Done with the screenshot, restore back the camera request
    reshade_addon_client_instance->restore_back_hunt_complete_camera_request();
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

    should_skip_camera_update = false;
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

ReShadeAddOnInjectClient::ReShadeAddOnInjectClient() {
    auto &api = reframework::API::get();

    // This is not stable
    auto action_controller_exec_action_method2 = api->tdb()->find_method("app.PlayerCameraController", "updateAction");
    action_controller_exec_action_method2->add_hook(pre_player_camera_controller_update_action,
        post_player_camera_controller_update_action, false);

    auto action_controller_exec_action_method = api->tdb()->find_method("app.PlayerCameraController", "<updateAction>g__exec|195_0(app.PlayerCameraController.ACTION_PART, System.Int32)");
    action_controller_exec_action_method->add_hook(pre_player_camera_controller_update_action,
        post_player_camera_controller_update_action, false);

    auto quest_result_start_method = api->tdb()->find_method("app.GUIFlowQuestResult.cContext", "onStartFlow");
    quest_result_start_method->add_hook(pre_open_quest_result_ui, null_post, false);

    auto quest_result_end_method = api->tdb()->find_method("app.GUIFlowQuestResult.cContext", "onEndFlow");
    quest_result_end_method->add_hook(pre_close_quest_result_ui, null_post, false);

    auto quest_success_free_playtime_on_enter_state_method = api->tdb()->find_method("app.cQuestSuccessFreePlayTime", "enter");
    quest_success_free_playtime_on_enter_state_method->add_hook(pre_quest_success_free_playtime_on_enter_state_proxy, post_quest_success_free_playtime_on_enter_state_proxy, false);

    // This is for quest failure
    auto hunt_repel_on_enter_state_method = api->tdb()->find_method("app.fsm_action.StEmCameraRepel", "doAction");
    hunt_repel_on_enter_state_method->add_hook(pre_quest_success_free_playtime_on_enter_state_proxy, post_quest_success_free_playtime_on_enter_state_proxy, false);

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

    prepare_state = CapturePrepareState::None;
    freeze_timescale_frame_left = -1;
    should_skip_camera_update = false;
}

ReShadeAddOnInjectClient::~ReShadeAddOnInjectClient() {
    if (reshade_module != nullptr) {
        FreeLibrary(reshade_module);
        reshade_module = nullptr;
    }

    if (webp_compress_thread != nullptr) {
        webp_compress_thread->join();
        webp_compress_thread.reset();
    }
}