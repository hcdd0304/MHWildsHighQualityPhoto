#pragma once

#include "../WebPCaptureInjectClient.hpp"
#include <reframework/API.hpp>

class GameProducedMaxQualityInjectClient : public WebPCaptureInjectClient {
private:
    static int pre_start_update_save_capture(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_start_update_save_capture(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    reframework::API *api = nullptr;
    reframework::API::ManagedObject *album_manager = nullptr;
    reframework::API::Method *get_serialized_field_content_method = nullptr;
    reframework::API::Method *get_completed_method = nullptr;

    std::vector<std::uint8_t> result_buffer;
    bool is_capture_done = false;
    int wait_frames_left = 0;
    ProvideFinishedDataCallback provide_data_finish_callback;

    void try_dump_webp();

    bool is_enabled = false;

public:
    explicit GameProducedMaxQualityInjectClient(reframework::API *api_instance);

    bool provide_webp_data(bool is16x9, ProvideFinishedDataCallback provide_data_finish_callback) override;

    static void initialize(reframework::API *api_instance);
    static GameProducedMaxQualityInjectClient *get_instance();

    void set_enable(bool is_enable) {
        this->is_enabled = is_enable;
    }

    bool is_enable() const {
        return this->is_enabled;
    }
};