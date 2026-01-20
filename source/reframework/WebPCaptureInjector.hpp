#pragma once

#include <reframework/API.hpp>
#include <atomic>
#include <functional>

class WebPCaptureInjectClient;

class WebPCaptureInjector {
private:
    WebPCaptureInjectClient *client = nullptr;
    reframework::API *api = nullptr;
    reframework::API::ManagedObject *album_manager = nullptr;
    reframework::API::ManagedObject *always_valid_array = nullptr;
    reframework::API::ManagedObject *original_webp_array = nullptr;

    reframework::API::Method *get_serialized_field_content_method = nullptr;
    reframework::API::Method *get_serialized_field_completed_method = nullptr;
    reframework::API::Method *get_serialized_field_valid_method = nullptr;
    reframework::API::TypeDefinition *byte_type = nullptr;
    reframework::API::Method *update_save_capture_method = nullptr;

    // Hook states

    bool has_request_capture = false;
    std::atomic_bool is_capture_done = true;
    bool has_injected = false;
    bool is_requesting_photo_save = false;
    bool request_processed = false;
    bool inject_pending = false;
    bool spoofed_result = false;

    reframework::API::ManagedObject *csave_obj = nullptr;

    std::unique_ptr<std::vector<std::uint8_t>> copied_buffer = nullptr;

    explicit WebPCaptureInjector(reframework::API *api_instance);

private:
    // Hooks
    static int pre_start_update_save_capture(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_start_update_save_capture(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_start_create_cphoto(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_start_create_cphoto(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_start_create_album_save_param(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_start_create_album_save_param(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_start_savedatamanager_system_requestphotosave(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_start_savedatamanager_system_requestphotosave(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_start_savedatamanager_system_request_hunter_profile_photo_save(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_start_savedatamanager_system_request_hunter_profile_photo_save(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_start_csave_request_ctor(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_start_csave_request_ctor(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_start_hunter_profile_photo_data_create(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_start_hunter_profile_photo_data_create(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_start_hunter_profile_photo_param_create(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_start_hunter_profile_photo_param_create(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);
    
    static int pre_album_manager_load_hunter_profile_photo(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_album_manager_load_hunter_profile_photo(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_cphoto_set_photo_data(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_cphoto_set_photo_data(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static void patch_cphoto(reframework::API::ManagedObject* cphoto);
    static void patch_hunter_profile_photo_data(reframework::API::ManagedObject* cphoto);

private:
    // Client callback
    void on_client_provide_webp_data(bool success, std::vector<std::uint8_t>* provided_data);
    void hook_to_extend_webp_max_size();

public:
    ~WebPCaptureInjector();

    static WebPCaptureInjector *get_instance();
    static void initialize(reframework::API *api_instance);

    void set_inject_client(WebPCaptureInjectClient *client) {
        this->client = client;
    }

    void set_inject_pending() {
        this->inject_pending = true;
    }
};