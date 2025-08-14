// Install DebugView to view the OutputDebugString calls
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define CIMGUI_NO_EXPORT

#include <sstream>
#include <mutex>
#include <cimgui.h>
#include <filesystem>

#undef API

#include "PluginBase.hpp"
#include "MHWildsTypes.h"
#include "ModSettings.hpp"
#include "WebPCaptureInjector.hpp"
#include "REFrameworkBorrowedAPI.hpp"

#include <nfd.hpp>

PluginBase *get_plugin_base_instance();

bool PluginBase::is_override_image_path_valid(const std::string& path, bool limit_size) {
    if (path.empty()) {
        return false;
    }

    if (!std::filesystem::exists(path)) {
        return false;
    }

    if (limit_size && std::filesystem::file_size(path) > MaxSerializePhotoSizeOriginal) {
        return false;
    }

    // No idea if this file is WebP or not, either way, the game will let us know (by corrupt cat icon)
    return true;
}

void PluginBase::open_blocking_webp_pick_file_dialog(std::string &file_path) {
    nfdu8char_t *out_path;
    nfdu8filteritem_t filters[2] = { { "WebP image", "webp" }, { "All files", "*" } };

    if (NFD::OpenDialog(out_path, filters, 2) == NFD_OKAY) {
        file_path = out_path;
        NFD::FreePath(out_path);
    } else {
        file_path.clear();
    }
}

void PluginBase::draw_user_interface_path(const std::string &label, std::string &target_path, bool limit_size) {
    igText(label.c_str());
    igSameLine(0.0f, 5.0f);

    std::string id_input = std::format("##{}", label);
    std::string name_button = std::format("Browse##{}", label);

    igInputText(id_input.c_str(), const_cast<char*>(target_path.c_str()), target_path.size() + 1,
        ImGuiInputTextFlags_ReadOnly, nullptr, nullptr);

    igSameLine(0.0f, 5.0f);

    if (igButton(name_button.c_str(), ImVec2(0, 0))) {
        open_blocking_webp_pick_file_dialog(target_path);
    }

    if (!target_path.empty()) {
        if (!is_override_image_path_valid(target_path, limit_size)) {
            std::string error_message = std::format("Your WebP image is either too big (size > {} KB), or corrupted or not exist anymore. Please recheck",
                MaxSerializePhotoSizeOriginal / 1024);

            igPushStyleColor_Vec4(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            igText(error_message.c_str());
            igPopStyleColor(1);
        }
    }
}

void PluginBase::base_initialize(PluginBase *plugin, const REFrameworkPluginInitializeParam *params, std::string_view settings_name,
    std::string_view debug_file_postfix) {
    auto persistent_dir = REFramework::get_persistent_dir();
    
    if (!std::filesystem::exists(persistent_dir)) {
        std::filesystem::create_directories(persistent_dir);
    }

    auto& api = reframework::API::get();

    /// THIS BELOW MUST BE FIRST
    ModSettings::initialize(settings_name);
    /// THIS ABOVE MUST BE FIRST

    WebPCaptureInjector::initialize(api.get());

    auto mod_settings = ModSettings::get_instance();
    if (mod_settings != nullptr) {
        mod_settings->debug_file_postfix = debug_file_postfix;
    }

    params->functions->on_imgui_draw_ui([](REFImGuiFrameCbData* data) {
        auto plugin = get_plugin_base_instance();

        if (plugin != nullptr) {
            plugin->draw_user_interface();
        }
    });

    params->functions->on_pre_application_entry("UpdateBehavior", []() {
        auto plugin = get_plugin_base_instance();

        if (plugin != nullptr) {
            plugin->update();
        }
    });

    params->functions->on_pre_application_entry("LateUpdateBehavior", []() {
        auto plugin = get_plugin_base_instance();

        if (plugin != nullptr) {
            plugin->late_update();
        }
    });

    params->functions->on_pre_application_entry("EndRendering", []() {
        auto plugin = get_plugin_base_instance();

        if (plugin != nullptr) {
            plugin->end_rendering();
        }
    });
}