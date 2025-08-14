#include "GameProducedMaxQualityInjectClient.hpp"
#include "../ModSettings.hpp"
#include "../MHWildsTypes.h"
#include "../REFrameworkBorrowedAPI.hpp"

#include <fstream>

std::unique_ptr<GameProducedMaxQualityInjectClient> game_max_quality_inject_client;

GameProducedMaxQualityInjectClient *GameProducedMaxQualityInjectClient::get_instance() {
    return game_max_quality_inject_client.get();
}

GameProducedMaxQualityInjectClient::GameProducedMaxQualityInjectClient(reframework::API *api_instance)
    : api(api_instance)
    , is_capture_done(false)
    , provide_data_finish_callback(nullptr) {
    auto tdb = api->tdb();
    
    auto update_save_capture_method = tdb->find_method("app.AlbumManager", "updateSaveCapture");
    if (!update_save_capture_method) {
        api->log_info("Can't find updateSaveCapture method");
        return;
    }

    album_manager = api->get_managed_singleton("app.AlbumManager");
    get_serialized_field_content_method = tdb->find_method("via.render.SerializedResult", "get_Content");
    get_completed_method = tdb->find_method("via.render.SerializedResult", "get_Completed");

    update_save_capture_method->add_hook(pre_start_update_save_capture, post_start_update_save_capture, false);
}

void GameProducedMaxQualityInjectClient::initialize(reframework::API *api_instance) {
    if (game_max_quality_inject_client) {
        api_instance->log_info("GameProducedMaxQualityInjectClient already initialized");
        return;
    }

    game_max_quality_inject_client = std::make_unique<GameProducedMaxQualityInjectClient>(api_instance);
    api_instance->log_info("GameProducedMaxQualityInjectClient initialized successfully");
}

void GameProducedMaxQualityInjectClient::try_dump_webp() {
    // Implement the logic to dump the WEBP image
    if (!is_capture_done) {
        api->log_info("Capture is not done yet, skipping WebP dump");
        return;
    }

    auto settings = ModSettings::get_instance();
    if (!settings) {
        api->log_info("Failed to get ModSettings instance");
        return;
    }

    if (settings->dump_mod_png) {
        static constexpr const char *DEBUG_FILE_NAME = "reframework/data/MHWilds_HighQualityPhotoMod_HighQuality_QuestResult.webp";

        auto persistent_dir = REFramework::get_persistent_dir();

        if (!std::filesystem::exists(persistent_dir)) {
            std::filesystem::create_directories(persistent_dir);
        }

        auto debug_path = persistent_dir / DEBUG_FILE_NAME;

        std::ofstream original(debug_path.string(), std::istream::out | std::istream::binary);
        original.write(reinterpret_cast<const char*>(result_buffer.data()), result_buffer.size());
        original.flush();
        original.close();
    }
}

int GameProducedMaxQualityInjectClient::pre_start_update_save_capture(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (game_max_quality_inject_client == nullptr) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    if (!game_max_quality_inject_client->is_enable()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    auto album_manager = game_max_quality_inject_client->album_manager;

    if (!album_manager) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    auto capture_state_ptr = album_manager->get_field<int>("_SaveCaptureState");
    auto api = game_max_quality_inject_client->api;

    const int MAX_QUALITY = 100;

    if (capture_state_ptr != nullptr) {
        SaveCaptureState capture_state = static_cast<SaveCaptureState>(*capture_state_ptr);

        auto mod_settings = ModSettings::get_instance();

        if (capture_state <= SAVECAPTURESTATE_START) {
            game_max_quality_inject_client->is_capture_done = false;
            game_max_quality_inject_client->wait_frames_left = mod_settings->hide_ui_before_capture_frame_count;
        }

        if (capture_state == SAVECAPTURESTATE_COPY_TO_STAGING) {
            // Reset quality to our standard
            auto quality_ptr = album_manager->get_field<std::uint32_t>("_Quality");
            if (quality_ptr && *quality_ptr < MAX_QUALITY) {
                api->log_info("Original capture quality: %d, updating to 100%", *quality_ptr);
                *quality_ptr = MAX_QUALITY;
            }

            game_max_quality_inject_client->wait_frames_left--;

            if (game_max_quality_inject_client->wait_frames_left > 0) {
                return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
            } else {
                api->log_info("Done delaying frames, proceeding with the original call");
            }
        }

        if (!game_max_quality_inject_client->is_capture_done && capture_state == SAVECAPTURESTATE_WAIT_SERIALIZE) {
            // Reset quality to our standard
            auto quality_ptr = album_manager->get_field<std::uint32_t>("_Quality");
            
            if (quality_ptr && *quality_ptr < MAX_QUALITY) {
                api->log_info("Capture quality: %d, skip dumping", *quality_ptr);
                return REFRAMEWORK_HOOK_CALL_ORIGINAL;
            }
            else {
                api->log_info("Capture quality: %d, dumping", *quality_ptr);
            }

            auto serialized_result = album_manager->get_field<reframework::API::ManagedObject*>("_SerializedResult");
            if (!serialized_result) {
                api->log_info("Can't find SerializedResult field");
                return REFRAMEWORK_HOOK_CALL_ORIGINAL;
            }

            auto vm_context = api->get_vm_context();
            auto is_serialize_completed = game_max_quality_inject_client->get_completed_method->call<bool>(api->get_vm_context(), *serialized_result);
            if (is_serialize_completed) {
                // Copy the data before the state checks and destroys it
                auto serialize_content = game_max_quality_inject_client->get_serialized_field_content_method->call
                    <reframework::API::ManagedObject*>(api->get_vm_context(), *serialized_result);

                if (!serialize_content) {
                    api->log_info("Unable to retrieve 100% WebP buffer");
                }
                else {
                    game_max_quality_inject_client->is_capture_done = true;

                    auto length = serialize_content->call<int>("GetLength", vm_context, serialize_content);
                    game_max_quality_inject_client->result_buffer.resize(length);
    
                    for (int i = 0; i < length; i++) {
                        game_max_quality_inject_client->result_buffer[i] = serialize_content->call<std::uint8_t>("Get", vm_context, serialize_content, i);
                    }

                    if (game_max_quality_inject_client->is_capture_done) {
                        game_max_quality_inject_client->try_dump_webp();
                    }

                    if (game_max_quality_inject_client->provide_data_finish_callback != nullptr) {
                        game_max_quality_inject_client->provide_data_finish_callback(true, &game_max_quality_inject_client->result_buffer);
                        game_max_quality_inject_client->is_capture_done = false;
                    }
                }
            }
        }
    }

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void GameProducedMaxQualityInjectClient::post_start_update_save_capture(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    
}

bool GameProducedMaxQualityInjectClient::provide_webp_data(bool is16x9, ProvideFinishedDataCallback provide_data_finish_callback) {
    if (!api) {
        return false;
    }

    if (is_capture_done) {
        provide_data_finish_callback(true, &result_buffer);
        is_capture_done = false;

        return true;
    }

    this->provide_data_finish_callback = std::move(provide_data_finish_callback);
    return true;
}