#include "WebPCaptureInjector.hpp"
#include "WebPCaptureInjectClient.hpp"
#include "MHWildsTypes.h"
#include "REFrameworkBorrowedAPI.hpp"
#include "ModSettings.hpp"

#include <fstream>
#include <format>

std::unique_ptr<WebPCaptureInjector> webp_capture_injector_instance = nullptr;

int WebPCaptureInjector::pre_start_update_save_capture(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (!webp_capture_injector_instance) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    auto album_manager = webp_capture_injector_instance->album_manager;

    if (!album_manager) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    auto capture_state_ptr = album_manager->get_field<int>("_SaveCaptureState");
    auto api = webp_capture_injector_instance->api;

    if (capture_state_ptr != nullptr) {
        SaveCaptureState capture_state = static_cast<SaveCaptureState>(*capture_state_ptr);

        if (webp_capture_injector_instance->has_request_capture) {
            if (capture_state == SAVECAPTURESTATE_WAIT_SERIALIZE && !webp_capture_injector_instance->is_capture_done) {
                return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
            }
        }
    }

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void WebPCaptureInjector::post_start_update_save_capture(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    if (!webp_capture_injector_instance) {
        return;
    }

    auto album_manager = webp_capture_injector_instance->album_manager;
    auto api = webp_capture_injector_instance->api;
    auto vm_context = api->get_vm_context();
    auto tdb = api->tdb();

    if (!album_manager || !api || !vm_context) {
        return;
    }
    
    auto capture_state_ptr = album_manager->get_field<int>("_SaveCaptureState");

    if (capture_state_ptr == nullptr) {
        api->log_info("Capture state null");
        return;
    } else {
        SaveCaptureState capture_state = static_cast<SaveCaptureState>(*capture_state_ptr);

        if (capture_state >= SAVECAPTURESTATE_START && webp_capture_injector_instance->inject_pending && !webp_capture_injector_instance->has_request_capture) {
            webp_capture_injector_instance->has_request_capture = true;
            webp_capture_injector_instance->is_capture_done = false;
            webp_capture_injector_instance->inject_pending = false;

            if (webp_capture_injector_instance->client) {
                auto func = std::bind(&WebPCaptureInjector::on_client_provide_webp_data, webp_capture_injector_instance.get(), std::placeholders::_1,
                    std::placeholders::_2);

                auto is16x9_ptr = album_manager->get_field<bool>("_Is16x9");

                if (!is16x9_ptr) {
                    api->log_error("Can't determine if the capture should be 16x9 or not. Default to true");
                }

                auto is16x9 = is16x9_ptr ? *is16x9_ptr : true;
                bool client_accept_capture_request = webp_capture_injector_instance->client->provide_webp_data(is16x9, func);
            
                if (!client_accept_capture_request) {
                    webp_capture_injector_instance->is_capture_done = true;
                }
            }
        }

        if (capture_state == SAVECAPTURESTATE_SAVE_CAPTURE && !webp_capture_injector_instance->has_injected &&
            webp_capture_injector_instance->is_capture_done &&
            webp_capture_injector_instance->copied_buffer != nullptr) {
            webp_capture_injector_instance->has_injected = true;

            auto serialized_result = album_manager->get_field<reframework::API::ManagedObject*>("_SerializedResult");
            if (!serialized_result) {
                api->log_info("Can't find SerializedResult field");
                return;
            }

            auto original_capture_data = webp_capture_injector_instance->get_serialized_field_content_method->call
                <reframework::API::ManagedObject*>(vm_context, *serialized_result);

            if (original_capture_data) {
                auto mod_settings = ModSettings::get_instance();
                if (mod_settings->dump_original_webp) {
                    std::vector<std::uint8_t> buffer;
                    auto length = original_capture_data->call<int>("GetLength", vm_context, original_capture_data);
                    buffer.resize(length);
    
                    for (int i = 0; i < length; i++) {
                        buffer[i] = original_capture_data->call<std::uint8_t>("Get", vm_context, original_capture_data, i);
                    }

                    static constexpr const char *DEBUG_FILE_NAME_FORMAT = "reframework/data/MHWilds_HighQualityPhotoMod_OriginalImage_{}.webp";
                    std::string debug_file_name = std::format(DEBUG_FILE_NAME_FORMAT, mod_settings->debug_file_postfix);

                    auto persistent_dir = REFramework::get_persistent_dir();

                    if (!std::filesystem::exists(persistent_dir)) {
                        std::filesystem::create_directories(persistent_dir);
                    }

                    auto debug_path = persistent_dir/ debug_file_name;

                    std::ofstream original(debug_path.string(), std::istream::out | std::istream::binary);
                    original.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
                    original.flush();
                    original.close();
                }

                original_capture_data->release();
            }

            auto& capture_buffer_ref = *webp_capture_injector_instance->copied_buffer;
            auto new_capture_data_array = api->create_managed_array(webp_capture_injector_instance->byte_type,
                static_cast<std::uint32_t>(capture_buffer_ref.size()));

            new_capture_data_array->add_ref();

            auto new_data_array_type = new_capture_data_array->get_type_definition();
            auto method_set = new_data_array_type->find_method("Set");

            for (int i = 0; i < capture_buffer_ref.size(); i++) {
                webp_capture_injector_instance->set_array_byte_method->call(vm_context, new_capture_data_array, i, capture_buffer_ref[i]);
            }

            api->log_info("Inject new WebP image finished!");

            *reinterpret_cast<reframework::API::ManagedObject**>(
                reinterpret_cast<std::uint8_t*>(*serialized_result) + SerializeResultContentArrayOffset) = new_capture_data_array;
        }

        if (webp_capture_injector_instance->has_request_capture && capture_state == SAVECAPTURESTATE_IDLE) {
            webp_capture_injector_instance->has_request_capture = false;
            webp_capture_injector_instance->is_capture_done = false;
            webp_capture_injector_instance->has_injected = false;
            webp_capture_injector_instance->copied_buffer.reset();
        }
    }
}

void WebPCaptureInjector::on_client_provide_webp_data(bool success, std::vector<std::uint8_t>* provided_data) {
    if (success && provided_data) {
        webp_capture_injector_instance->copied_buffer = std::make_unique<std::vector<std::uint8_t>>(*provided_data);
        api->log_info("Capture finished successfully! on capture injector");
    } else {
        api->log_info("Capture failed! on capture injector");
    }
    webp_capture_injector_instance->is_capture_done = true;
}

int WebPCaptureInjector::pre_start_create_cphoto(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void WebPCaptureInjector::patch_cphoto(reframework::API::ManagedObject* cphoto) {
    if (!cphoto) {
        return;
    }

    auto data_array_ptr = cphoto->get_field<reframework::API::ManagedObject*>("SerializeData");

    if (!data_array_ptr) {
        return;
    }

    auto data_array = *data_array_ptr;
    auto array_length = data_array->call<int>("GetLength", webp_capture_injector_instance->api->get_vm_context(), data_array);

    if (array_length >= MaxSerializePhotoSize) {
        return;
    }

    data_array->release();

    auto new_data_array = webp_capture_injector_instance->api->create_managed_array(webp_capture_injector_instance->byte_type, MaxSerializePhotoSize);
    new_data_array->add_ref();

    *data_array_ptr = new_data_array;
}

void WebPCaptureInjector::patch_hunter_profile_photo_data(reframework::API::ManagedObject* cprofile_photo_savedata) {
    if (!cprofile_photo_savedata) {
        return;
    }

    auto cprofile_photo_save_param_obj_ptr = cprofile_photo_savedata->get_field<reframework::API::ManagedObject*>("_Data");

    if (!cprofile_photo_save_param_obj_ptr) {
        return;
    }

    auto cprofile_photo_save_param_obj = *cprofile_photo_save_param_obj_ptr;
    auto cphoto_ptr = cprofile_photo_save_param_obj->get_field<reframework::API::ManagedObject*>("_PhotoData");

    if (!cphoto_ptr) {
        return;
    }

    auto cphoto = *cphoto_ptr;
    patch_cphoto(cphoto);
}

void WebPCaptureInjector::post_start_create_cphoto(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    auto cphoto_ptr = reinterpret_cast<reframework::API::ManagedObject**>(ret_val);
    if (!cphoto_ptr) {
        return;
    }

    auto cphoto = *cphoto_ptr;
    patch_cphoto(cphoto);
}

int WebPCaptureInjector::pre_start_hunter_profile_photo_data_create(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void WebPCaptureInjector::post_start_hunter_profile_photo_data_create(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    auto cprofile_photo_savedata_ptr = reinterpret_cast<reframework::API::ManagedObject**>(ret_val);
    if (!cprofile_photo_savedata_ptr) {
        return;
    }

    patch_hunter_profile_photo_data(*cprofile_photo_savedata_ptr);
}

int WebPCaptureInjector::pre_start_hunter_profile_photo_param_create(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void WebPCaptureInjector::post_start_hunter_profile_photo_param_create(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    auto cprofile_photo_save_param_obj_ptr = reinterpret_cast<reframework::API::ManagedObject**>(ret_val);

    if (!cprofile_photo_save_param_obj_ptr) {
        return;
    }

    auto cprofile_photo_save_param_obj = *cprofile_photo_save_param_obj_ptr;
    auto cphoto_ptr = cprofile_photo_save_param_obj->get_field<reframework::API::ManagedObject*>("_PhotoData");

    if (!cphoto_ptr) {
        return;
    }

    auto cphoto = *cphoto_ptr;
    patch_cphoto(cphoto);
}

int WebPCaptureInjector::pre_album_manager_load_hunter_profile_photo(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void WebPCaptureInjector::post_album_manager_load_hunter_profile_photo(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    // Luckly the save data manager has not managed to load the array (YET)
    auto album_manager = webp_capture_injector_instance->album_manager;
    auto hunter_profile_photo_data_ptr = album_manager->get_field<reframework::API::ManagedObject*>("_HunterProfilePhotoDataCache");

    if (!hunter_profile_photo_data_ptr) {
        webp_capture_injector_instance->api->log_info("Hunter profile photo data field is null");
        return;
    }

    auto hunter_profile_photo_data = *hunter_profile_photo_data_ptr;
    patch_hunter_profile_photo_data(hunter_profile_photo_data);
}

int WebPCaptureInjector::pre_start_create_album_save_param(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void WebPCaptureInjector::post_start_create_album_save_param(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    auto album_save_param_ptr = reinterpret_cast<reframework::API::ManagedObject**>(ret_val);

    if (!album_save_param_ptr) {
        return;
    }

    auto album_save_param = *album_save_param_ptr;
    auto photo_datas_ptr = album_save_param->get_field<reframework::API::ManagedObject*>("_PhotoDatas");

    if (!photo_datas_ptr) {
        webp_capture_injector_instance->api->log_info("Photo datas field is null");
        return;
    }

    auto vm_context = webp_capture_injector_instance->api->get_vm_context();
    auto photo_datas = *photo_datas_ptr;

    auto photo_data_length = photo_datas->call<int>("GetLength", vm_context, photo_datas);
    for (int i = 0; i < photo_data_length; i++) {
        auto photo_data = photo_datas->call<reframework::API::ManagedObject*>("Get", vm_context, photo_datas, i);
        auto photo_data_ptr = photo_data->get_field<reframework::API::ManagedObject*>("SerializeData");

        if (!photo_data_ptr) {
            webp_capture_injector_instance->api->log_info("Serialize data field of cAlbumSaveParam.PhotoData is null");
            continue;
        }

        auto old_photo_data_array = *photo_data_ptr;
        old_photo_data_array->release();

        auto new_photo_data_array = webp_capture_injector_instance->api->create_managed_array(
            webp_capture_injector_instance->byte_type, MaxSerializePhotoSize);
        
        new_photo_data_array->add_ref();
        *photo_data_ptr = new_photo_data_array;
    }
}

int WebPCaptureInjector::pre_start_savedatamanager_system_requestphotosave(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (!webp_capture_injector_instance) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    webp_capture_injector_instance->is_requesting_photo_save = true;
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void WebPCaptureInjector::post_start_savedatamanager_system_requestphotosave(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    
}

int WebPCaptureInjector::pre_start_savedatamanager_system_request_hunter_profile_photo_save(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    return pre_start_savedatamanager_system_requestphotosave(argc, argv, arg_tys, ret_addr);
}

void WebPCaptureInjector::post_start_savedatamanager_system_request_hunter_profile_photo_save(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {

}

int WebPCaptureInjector::pre_start_csave_request_ctor(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (webp_capture_injector_instance) {
        webp_capture_injector_instance->csave_obj = reinterpret_cast<reframework::API::ManagedObject*>(argv[1]);
    }
    
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void WebPCaptureInjector::post_start_csave_request_ctor(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    if (!webp_capture_injector_instance) {
        return;
    }
   
    if (webp_capture_injector_instance->is_requesting_photo_save) {
        if (webp_capture_injector_instance->csave_obj != nullptr) {
            // Leave a little room for grow (in case there is)
            webp_capture_injector_instance->csave_obj->call("set_TargetSize", webp_capture_injector_instance->api->get_vm_context(),
                webp_capture_injector_instance->csave_obj,
                (MaxSerializePhotoSize / 2 * 3));
        } else {
            webp_capture_injector_instance->api->log_info("CSaveRequest is null");
        }

        webp_capture_injector_instance->is_requesting_photo_save = false;
    }
}

int WebPCaptureInjector::pre_cphoto_set_photo_data(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    auto cphoto_obj = reinterpret_cast<reframework::API::ManagedObject*>(argv[1]);
    patch_cphoto(cphoto_obj);

    webp_capture_injector_instance->api->log_info("Set photo data called, changing size beforehand");

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void WebPCaptureInjector::post_cphoto_set_photo_data(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {

}

WebPCaptureInjector::WebPCaptureInjector(reframework::API *api)
    : api(api)
    , album_manager(nullptr) {
    auto tdb = api->tdb();

    album_manager = api->get_managed_singleton("app.AlbumManager");
    get_serialized_field_content_method = tdb->find_method("via.render.SerializedResult", "get_Content");
    set_array_byte_method = tdb->find_method("System.Byte[]", "Set");
    byte_type = tdb->find_type("System.Byte");
    
    auto update_save_capture_method = tdb->find_method("app.AlbumManager", "updateSaveCapture");
    if (!update_save_capture_method) {
        api->log_info("Can't find updateSaveCapture method");
        return;
    }

    update_save_capture_method->add_hook(pre_start_update_save_capture, post_start_update_save_capture, false);
    //hook_to_extend_webp_max_size();
}

void WebPCaptureInjector::hook_to_extend_webp_max_size() {
    auto tdb = api->tdb();

    auto create_cphoto_method = tdb->find_method("app.savedata.cPhoto", "create");
    create_cphoto_method->add_hook(pre_start_create_cphoto, post_start_create_cphoto, false);

    auto create_album_save_param_method = tdb->find_method("app.savedata.cAlbumSaveParam", "create");
    create_album_save_param_method->add_hook(pre_start_create_album_save_param, post_start_create_album_save_param, false);

    auto photo_save_method = tdb->find_method("app.SaveDataManager", "systemRequestPhotoSave");
    photo_save_method->add_hook(pre_start_savedatamanager_system_requestphotosave, post_start_savedatamanager_system_requestphotosave, false);

    auto hunter_profile_photo_save_method = tdb->find_method("app.SaveDataManager", "systemRequestHunterProfilePhotoSave");
    hunter_profile_photo_save_method->add_hook(pre_start_savedatamanager_system_request_hunter_profile_photo_save, post_start_savedatamanager_system_request_hunter_profile_photo_save, false);

    auto csave_request_method = tdb->find_method("ace.SaveDataManagerBase.cSaveRequest", ".ctor");
    csave_request_method->add_hook(pre_start_csave_request_ctor, post_start_csave_request_ctor, false);

    auto hunter_profile_photo_data_create_method = tdb->find_method("app.savedata.cHunterProfilePhotoData", "create");
    hunter_profile_photo_data_create_method->add_hook(pre_start_hunter_profile_photo_data_create, post_start_hunter_profile_photo_data_create, false);

    auto hunter_profile_photo_param_create_method = tdb->find_method("app.savedata.cHunterProfilePhotoParam", "create");
    hunter_profile_photo_param_create_method->add_hook(pre_start_hunter_profile_photo_param_create, post_start_hunter_profile_photo_param_create, false);

    auto load_hunter_profile_photo_method = tdb->find_method("app.AlbumManager", "loadHunterProfilePhoto");
    load_hunter_profile_photo_method->add_hook(pre_album_manager_load_hunter_profile_photo, post_album_manager_load_hunter_profile_photo, false);

    auto cphoto_set_photo_data_method = tdb->find_method("app.savedata.cPhoto", "setPhotoData");
    cphoto_set_photo_data_method->add_hook(pre_cphoto_set_photo_data, post_cphoto_set_photo_data, false);
}

WebPCaptureInjector *WebPCaptureInjector::get_instance() {
    return webp_capture_injector_instance ? webp_capture_injector_instance.get() : nullptr;
}

void WebPCaptureInjector::initialize(reframework::API *api_instance) {
    if (webp_capture_injector_instance == nullptr) {
        webp_capture_injector_instance = std::unique_ptr<WebPCaptureInjector>(new WebPCaptureInjector(api_instance));
    }
}