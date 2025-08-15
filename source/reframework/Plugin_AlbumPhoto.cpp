// Install DebugView to view the OutputDebugString calls
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define CIMGUI_NO_EXPORT

#include <sstream>
#include <mutex>
#include <cimgui.h>
#include <filesystem>

#undef API

#include "Plugin_AlbumPhoto.hpp"
#include "MHWildsTypes.h"
#include "ModSettings.hpp"
#include "WebPCaptureInjector.hpp"
#include "REFrameworkBorrowedAPI.hpp"

#include <nfd.hpp>

std::unique_ptr<Plugin_AlbumPhoto> plugin_instance = nullptr;

PluginBase *get_plugin_base_instance() {
    return reinterpret_cast<PluginBase*>(plugin_instance.get());
}

void Plugin_AlbumPhoto::set_capture_again() {
    auto mod_settings = ModSettings::get_instance();
    auto injector = WebPCaptureInjector::get_instance();

    if (mod_settings == nullptr || injector == nullptr) {
        return;
    }

    if (mod_settings->disable_mod || !mod_settings->enable_override_album_image) {
        // No need to set inject pending
        injector->set_inject_client(null_capture_client.get());
        return;
    }

    album_photo_force_client->set_requested();
    album_photo_force_client->set_limit_size(true);
    album_photo_force_client->set_file_path(mod_settings->override_album_image_path);

    injector->set_inject_client(album_photo_force_client.get());
    injector->set_inject_pending();
}

int Plugin_AlbumPhoto::pre_save_capture_photo_hook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    auto mod_settings = ModSettings::get_instance();

    if (mod_settings == nullptr) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    plugin_instance->set_capture_again();
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void Plugin_AlbumPhoto::post_save_capture_photo_hook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {

}

void Plugin_AlbumPhoto::update() {
}

void Plugin_AlbumPhoto::late_update() {
}

void Plugin_AlbumPhoto::end_rendering() {
}

static void igTextBulletWrapped(const char *bullet, const char *text) {
    igTextWrapped(bullet);
    igSameLine(0.0f, 5.0f);
    igTextWrapped(text);
}

void Plugin_AlbumPhoto::draw_user_interface() {
    if (igCollapsingHeader_TreeNodeFlags("Custom Photos", ImGuiTreeNodeFlags_None)) {
        auto mod_settings = ModSettings::get_instance();
        auto mod_settings_copy = *mod_settings;

        if (igTreeNode_Str("Help")) {
            if (igTreeNode_Str("How to upload custom image to hunter profile")) {
                igTextBulletWrapped("1.", "Go to the \"Photo mode/Album\" section and enable \"Custom Album Image\" option.");
                igTextBulletWrapped("2.", "Take an image in Photo Mode. Your custom image will then be saved in the \"Album\".");
                igTextBulletWrapped("3.", "Go to your Hunter Profile, choose the custom image just saved in the \"Album\".");
                igTreePop();
            }

            igTreePop();
        }

        if (igTreeNode_Str("General")) {
            igCheckbox("Disable Mod ##DisableMod", &mod_settings->disable_mod);

            igSeparator();

            igCheckbox("Enable Custom Album Image", &mod_settings->enable_override_album_image);

            if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                igSetTooltip("Your photo mode result will be a image you selected. You can use this to upload to hunter profile");
            }
    
            if (mod_settings->enable_override_album_image) {
                igPushStyleColor_Vec4(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                igTextWrapped("NOTE: Please adjust your custom image to be in WebP format. The image dimension should be 1920x1080 and file size <= 256KB.");
                igPopStyleColor(1);

                draw_user_interface_path("Album Image Path", mod_settings->override_album_image_path, true);
            }

            igTreePop();
        }

        if (igTreeNode_Str("Debug")) {
            igCheckbox("Dump Original WebP image##DumpWebPAlbumPhoto", &mod_settings->dump_original_webp);
            if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                igSetTooltip("This will dump the original WebP image the game made to the game directory.");
            }

            igText("Path to WebP: <GameDir>/reframework/data/MHWilds_HighQualityPhotoMod_OriginalImage_PhotoMode.webp");
            igTreePop();
        }

        mod_settings->max_album_image_quality = std::clamp(mod_settings->max_album_image_quality, 10, 100);
        mod_settings->hdr_bits = std::clamp(mod_settings->hdr_bits, 10, 20);

        if (mod_settings->data_changed(mod_settings_copy)) {
            mod_settings->save();
        }
    }
}

Plugin_AlbumPhoto::Plugin_AlbumPhoto(const REFrameworkPluginInitializeParam *params) {
    auto& api = reframework::API::get();
    auto tdb = api->tdb();

    auto save_capture_method = tdb->find_method("app.AlbumManager", "saveCapturePhoto");
    save_capture_method->add_hook(pre_save_capture_photo_hook, post_save_capture_photo_hook, false);

    album_photo_force_client = std::make_unique<FileInjectClient>();
    null_capture_client = std::make_unique<NullCaptureInjectClient>();

    album_photo_force_client->set_limit_size(true);
}

void Plugin_AlbumPhoto::initialize(const REFrameworkPluginInitializeParam *params) {
    static const char *SETTINGS_NAME = "mhwilds_custom_album_photo";
    static const char *DEBUG_FILE_POSTFIX = "PhotoMode";

    plugin_instance = std::make_unique<Plugin_AlbumPhoto>(params);
    PluginBase::base_initialize(plugin_instance.get(), params, SETTINGS_NAME, DEBUG_FILE_POSTFIX);
}

Plugin_AlbumPhoto *Plugin_AlbumPhoto::get_instance() {
    return plugin_instance ? plugin_instance.get() : nullptr;
}

extern "C" __declspec(dllexport) void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;

    // Optionally, specify a specific game name that this plugin is compatible with.
    version->game_name = "MHWILDS";
}

extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    reframework::API::initialize(param);
    Plugin_AlbumPhoto::initialize(param);

    return true;
}
