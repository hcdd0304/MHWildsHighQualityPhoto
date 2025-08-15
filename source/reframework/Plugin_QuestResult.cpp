// Install DebugView to view the OutputDebugString calls
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define CIMGUI_NO_EXPORT

#include <sstream>
#include <mutex>
#include <cimgui.h>
#include <filesystem>

#undef API

#include "Plugin_QuestResult.hpp"
#include "MHWildsTypes.h"
#include "ModSettings.hpp"
#include "InjectClient/ReShadeAddOnInjectClient.hpp"
#include "InjectClient/GameProducedMaxQualityInjectClient.hpp"
#include "WebPCaptureInjector.hpp"
#include "REFrameworkBorrowedAPI.hpp"
#include "CaptureResolutionInject.hpp"

#include "GameUIController.hpp"

#include <nfd.hpp>

std::unique_ptr<Plugin_QuestResult> plugin_instance = nullptr;

PluginBase *get_plugin_base_instance() {
    return reinterpret_cast<PluginBase*>(plugin_instance.get());
}

void Plugin_QuestResult::decide_and_set_screen_cap_or_override_inject(std::unique_ptr<FileInjectClient> &override_client,
    bool should_override,
    const std::string &override_path,
    bool should_lossless,
    bool is_photo_mode) {
    auto mod_settings = ModSettings::get_instance();
    auto injector = WebPCaptureInjector::get_instance();

    if (mod_settings == nullptr || injector == nullptr) {
        return;
    }

    auto reshade_addon_client = ReShadeAddOnInjectClient::get_instance();
    if (mod_settings->disable_mod) {
        // No need to set inject pending
        reshade_addon_client->set_enable(false);
        injector->set_inject_client(null_capture_client.get());
    
        return;
    }

    reshade_addon_client->set_enable(!mod_settings->is_disable_high_quality_screen_capture());
    reshade_addon_client->set_lossless(should_lossless);
    reshade_addon_client->set_use_old_limit_size(false);
    reshade_addon_client->set_hq_background_mode(mod_settings->quest_result_hq_background_mode);

    if (is_photo_mode) {
        reshade_addon_client->set_is_photo_mode(true);

        if (!mod_settings->is_disable_high_quality_screen_capture()) {
            if (mod_settings->get_photo_mode_image_quality() == PhotoModeImageQuality_DoNotModify) {
                reshade_addon_client->set_enable(false);
            }
        }

        if (mod_settings->get_photo_mode_image_quality() == PhotoModeImageQuality_LowQualityApplyFilters) {
            reshade_addon_client->set_use_old_limit_size(true);
        }
    } else {
        reshade_addon_client->set_is_photo_mode(false);
    }

    injector->set_inject_client(reshade_addon_client);

    auto game_max_quality_inject_client = GameProducedMaxQualityInjectClient::get_instance();
    game_max_quality_inject_client->set_enable(false);

    bool is_overriden = false;

    if (should_override) {
        if (is_override_image_path_valid(override_path, is_photo_mode)) {
            override_client->set_file_path(override_path);
            override_client->set_requested();

            injector->set_inject_client(override_client.get());

            reshade_addon_client->set_enable(false);
            
            is_overriden = true;
        }
    }
    else {
        if (!is_photo_mode) {
            if (mod_settings->quest_result_hq_background_mode == QuestResultHQBackgroundMode::REEngineFrame) {
                reshade_addon_client->set_enable(false);
                game_max_quality_inject_client->set_enable(true);

                injector->set_inject_client(game_max_quality_inject_client);
            }
        }
    }

    auto resolution_inject = CaptureResolutionInject::get_instance();

    if (!is_overriden) {
        if (mod_settings->quest_result_hq_background_mode != QuestResultHQBackgroundMode::REEngineFrame) {
            reshade_addon_client->set_requested();
        }
        resolution_inject->update_resolution();
    } else {
        resolution_inject->revert();
    }

    injector->set_inject_pending();
}

int Plugin_QuestResult::pre_quest_failure_hook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    auto mod_settings = ModSettings::get_instance();

    if (mod_settings == nullptr || plugin_instance == nullptr) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    plugin_instance->decide_and_set_screen_cap_or_override_inject(plugin_instance->quest_failure_force_client,
        mod_settings->enable_override_quest_failure,
        mod_settings->override_quest_failure_background_path,
        mod_settings->get_use_lossless_image_for_quest_result(),
        false);

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void Plugin_QuestResult::post_quest_failure_hook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {

}

int Plugin_QuestResult::pre_quest_success_hook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    auto mod_settings = ModSettings::get_instance();

    if (mod_settings == nullptr || plugin_instance == nullptr) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    plugin_instance->decide_and_set_screen_cap_or_override_inject(plugin_instance->quest_success_force_client,
        mod_settings->enable_override_quest_success,
        mod_settings->override_quest_complete_background_path,
        mod_settings->get_use_lossless_image_for_quest_result(),
        false);

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void Plugin_QuestResult::post_quest_success_hook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {

}

int Plugin_QuestResult::pre_quest_cancel_hook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    auto mod_settings = ModSettings::get_instance();

    if (mod_settings == nullptr || plugin_instance == nullptr) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    plugin_instance->decide_and_set_screen_cap_or_override_inject(plugin_instance->quest_cancel_force_client,
        mod_settings->enable_override_quest_cancel,
        mod_settings->override_quest_headback_background_path,
        mod_settings->get_use_lossless_image_for_quest_result(),
        false);

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void Plugin_QuestResult::post_quest_cancel_hook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {

}

int Plugin_QuestResult::pre_save_capture_photo_hook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    auto resolution_inject = CaptureResolutionInject::get_instance();
    if (resolution_inject != nullptr) {
        resolution_inject->revert();
    }

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void Plugin_QuestResult::post_save_capture_photo_hook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    // No operation
}

int Plugin_QuestResult::pre_open_quest_result_ui(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    auto game_ui_controller = GameUIController::get_instance();
    auto settings = ModSettings::get_instance();

    auto &api = reframework::API::get();
    game_ui_controller->set_is_in_quest_result(true);

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int Plugin_QuestResult::pre_close_quest_result_ui(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    auto game_ui_controller = GameUIController::get_instance();
    game_ui_controller->set_is_in_quest_result(false);

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void Plugin_QuestResult::null_post(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    // No operation
}

void Plugin_QuestResult::update() {
    auto game_ui_controller_instance = GameUIController::get_instance();
    if (game_ui_controller_instance != nullptr) {
        game_ui_controller_instance->update();
    }

    auto reshade_addon_client = ReShadeAddOnInjectClient::get_instance();
    if (reshade_addon_client != nullptr) {
        reshade_addon_client->update();
    }
}

void Plugin_QuestResult::late_update() {
    auto reshade_addon_client = ReShadeAddOnInjectClient::get_instance();
    if (reshade_addon_client != nullptr) {
        reshade_addon_client->late_update();
    }
}

void Plugin_QuestResult::end_rendering() {
    auto reshade_addon_client = ReShadeAddOnInjectClient::get_instance();
    if (reshade_addon_client != nullptr) {
        reshade_addon_client->end_rendering();
    }
}

static const char *get_quest_result_hq_background_mode_name(QuestResultHQBackgroundMode mode) {
    switch (mode) {
        case ReshadePreapplied:
            return "ReShade Preapplied";
        case NoReshade:
            return "No ReShade";
        case ReshadeApplyLater:
            return "Normal (ReShade Apply Later)";
        case REEngineFrame:
            return "REEngine Frame";
        default:
            return "Unknown";
    }
}

static void igTextBulletWrapped(const char *bullet, const char *text) {
    igTextWrapped(bullet);
    igSameLine(0.0f, 5.0f);
    igTextWrapped(text);
}

bool test_timescale = false;

void Plugin_QuestResult::draw_user_interface() {
    if (igCollapsingHeader_TreeNodeFlags("HQ Quest Result Background", ImGuiTreeNodeFlags_None)) {
        auto mod_settings = ModSettings::get_instance();
        auto mod_settings_copy = *mod_settings;

        igPushStyleColor_Vec4(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
        igTextWrapped("WARNING: HDR not tested on a real HDR monitor. Do not enable this mod when use 21:9 on a 16:9 monitor, your high quality photo will have black bars on top and bottom");
        igPopStyleColor(1);

        auto reshade_addon_client = ReShadeAddOnInjectClient::get_instance();
        if (reshade_addon_client == nullptr || !reshade_addon_client->is_reshade_present()) {
            igPushStyleColor_Vec4(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
            igTextWrapped("WARNING: ReShade with Add-On is not present. High quality image capture will not work. If you want high quality capture to work, please install Reshade WITH ADD-ON (CAPS LOCK TEXT IMPORTANT)");
            igPopStyleColor(1);
        }

        igPushStyleColor_Vec4(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        igTextWrapped("NOTE: If the UI is included in your quest result, try increase the \"Frames hide UI before capture\" value higher until you no longer sees the UI in your quest result background.");
        igPopStyleColor(1);

        igSeparator();

        /*
        if (igTreeNode_Str("Help")) {
            igTreePop();
        }*/

        if (igTreeNode_Str("General")) {
            igCheckbox("Disable Mod ##DisableModHQQuestBg", &mod_settings->disable_mod);

            igText("HDR Bits");
            if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                igSetTooltip("The HDR bits used for screen capture. Valid on HDR monitor");
            }

            igSameLine(0.0f, 5.0f);
            igInputInt("##HDRBits", &mod_settings->hdr_bits, 1, 1, ImGuiInputTextFlags_None);

            igSeparator();

            igPushStyleColor_Vec4(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            igTextWrapped("NOTE: Please adjust your custom image to be in WebP format. The image dimension should be 1920x1080 on 16x9 monitor and 2560x1080 on 21x9 monitor. File size is not limited");
            igPopStyleColor(1);

            igCheckbox("Enable Custom Quest Failure Image", &mod_settings->enable_override_quest_failure);
    
            if (mod_settings->enable_override_quest_failure) {
                draw_user_interface_path("Quest Failure Image Path", mod_settings->override_quest_failure_background_path, false);
            }
    
            igCheckbox("Enable Custom Quest Success Image", &mod_settings->enable_override_quest_success);
    
            if (mod_settings->enable_override_quest_success) {
                draw_user_interface_path("Quest Success Image Path", mod_settings->override_quest_complete_background_path, false);
            }
    
            igCheckbox("Enable Custom Quest Cancel Image", &mod_settings->enable_override_quest_cancel);
    
            if (mod_settings->enable_override_quest_cancel) {
                draw_user_interface_path("Quest Cancel Image Path", mod_settings->override_quest_headback_background_path, false);
            }

            igSeparator();

            igText("Quest Result HQ Background Mode");
            igSameLine(0.0f, 5.0f);

            if (igBeginCombo("##QuestResultHQBackgroundMode", get_quest_result_hq_background_mode_name(mod_settings->quest_result_hq_background_mode), ImGuiComboFlags_None)) {
                bool selected = false;

                if (igSelectable_BoolPtr(get_quest_result_hq_background_mode_name(QuestResultHQBackgroundMode::REEngineFrame), &selected, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                    mod_settings->quest_result_hq_background_mode = REEngineFrame;
                }

                if (igSelectable_BoolPtr(get_quest_result_hq_background_mode_name(QuestResultHQBackgroundMode::ReshadeApplyLater), &selected, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                    mod_settings->quest_result_hq_background_mode = ReshadeApplyLater;
                }

                if (igSelectable_BoolPtr(get_quest_result_hq_background_mode_name(QuestResultHQBackgroundMode::ReshadePreapplied), &selected, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                    mod_settings->quest_result_hq_background_mode = ReshadePreapplied;
                }

                if (igSelectable_BoolPtr(get_quest_result_hq_background_mode_name(QuestResultHQBackgroundMode::NoReshade), &selected, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                    mod_settings->quest_result_hq_background_mode = NoReshade;
                }

                igEndCombo();
            }

            if (mod_settings->quest_result_hq_background_mode == REEngineFrame) {
                igTextWrapped("The mod forces the game to take a screenshot in maximum quality. The resulting image will still be affected by compression, but it's lighter than usual.");
                igTextWrapped("Artifacts caused by frame generation will not appear in this mode.");
            }

            if (mod_settings->quest_result_hq_background_mode == ReshadePreapplied) {
                igTextWrapped("The mod will take a screenshot with ReShade applied. When this screenshot is shown on quest result screen, ReShade will be disabled.");
                igTextWrapped("This ensures some ReShade effects that relies on the depth buffer displays correctly. Use this if you have problem with the default mode.");
                igTextWrapped("Turn on Fix Frame Generation Artifacts (on by default) to fix artifacts caused by frame generation.");
            }

            if (mod_settings->quest_result_hq_background_mode == ReshadeApplyLater) {
                igTextWrapped("The mod will take a screenshot without ReShade. When this screenshot is shown on quest result screen, the image will be shown with ReShade applied.");
                igTextWrapped("Turn on Fix Frame Generation Artifacts (on by default) to fix artifacts caused by frame generation.");
            }

            if (mod_settings->quest_result_hq_background_mode == NoReshade) {
                igTextWrapped("The mod will take a screenshot without ReShade. When this screenshot is shown on quest result screen, the image will be shown without ReShade applied.");
                igTextWrapped("Turn on Fix Frame Generation Artifacts (on by default) to fix artifacts caused by frame generation.");
            }

            igSeparator();

            igText("Frames hide UI before capture");
            igSameLine(0.0f, 5.0f);
            igInputInt("##HideUIBeforeCaptureFrameCount", &mod_settings->hide_ui_before_capture_frame_count, 1, 1, ImGuiInputTextFlags_None);

            igTextWrapped("The mod needs to hide the UI to take a screnshot of your game.");
            igTextWrapped("The amount of frames from when the mod requesting the game to hide the UI, to when the UI is actually hidden on the screen, may be inconsistent.");
            igTextWrapped("If the UI is included in your quest result, try increase this value higher until you no longer sees the UI in your quest result background.");

            igCheckbox("Hide Chat Notification", &mod_settings->hide_chat_notification);
            if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                igSetTooltip("Hide the chat icon on the top-right of your quest result screen");
            }

            igCheckbox("Fix Quest Result Brightness", &mod_settings->auto_fix_quest_result_brightness);
            if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                igSetTooltip("This will temporarily revert the UI brightness to neutral, until you exit the quest result screen, so that the quest result image is displayed with the same color tone as gameplay.");
            }

            if (igTreeNode_Str("Fix Frame Generation Artifacts")) {
                igTextWrapped("The mod will freeze your game for a few frames, so that the image generated by framegen becomes stable.");
            
                igCheckbox("Enable", &mod_settings->fix_framegen_artifacts);

                igText("Freeze Game Frame Count");
                igSameLine(0.0f, 5.0f);
                igInputInt("##FreezeGameFrameCount", &mod_settings->freeze_game_frames, 1, 1, ImGuiInputTextFlags_None);

                igTreePop();
            }

            igTreePop();
        }

        if (igTreeNode_Str("Debug")) {
            igCheckbox("Dump Original WebP image##DumpWebpImageQR", &mod_settings->dump_original_webp);
            if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                igSetTooltip("This will dump the original WebP image the game made to the game directory.");
            }

            igText("Path to WebP: <GameDir>/reframework/data/MHWilds_HighQualityPhotoMod_OriginalImage_QuestResult.webp");

            igCheckbox("Dump Mod-Captured PNG image##DumPngImageQR", &mod_settings->dump_mod_png);
            if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                igSetTooltip("This will dump the the PNG screenshot the mod captured to the game directory.");
            }

            igText("Path to WebP: <GameDir>/reframework/data/MHWilds_HighQualityPhotoMod_HighQuality_QuestResult.png/webp");
            igTreePop();
        }

        mod_settings->max_album_image_quality = std::clamp(mod_settings->max_album_image_quality, 10, 100);
        mod_settings->hdr_bits = std::clamp(mod_settings->hdr_bits, 10, 20);
        mod_settings->hide_ui_before_capture_frame_count = std::clamp(mod_settings->hide_ui_before_capture_frame_count, 3, 20);
        mod_settings->freeze_game_frames = std::clamp(mod_settings->freeze_game_frames, ReShadeAddOnInjectClient::MIN_FREEZE_TIMESCALE_FRAME_COUNT,
            ReShadeAddOnInjectClient::MAX_FREEZE_TIMESCALE_FRAME_COUNT);

        if (mod_settings->data_changed(mod_settings_copy)) {
            mod_settings->save();
        }
    }
}

Plugin_QuestResult::Plugin_QuestResult(const REFrameworkPluginInitializeParam *params) {
    auto& api = reframework::API::get();
    auto tdb = api->tdb();

    auto quest_failed_method = tdb->find_method("app.cQuestFailed", "enter");
    quest_failed_method->add_hook(pre_quest_failure_hook, post_quest_failure_hook, false);

    auto quest_success_method = tdb->find_method("app.cQuestClear", "enter");
    quest_success_method->add_hook(pre_quest_success_hook, post_quest_success_hook, false);

    auto quest_cancel_method = tdb->find_method("app.cQuestCancel", "enter");
    quest_cancel_method->add_hook(pre_quest_cancel_hook, post_quest_cancel_hook, false);

    auto save_capture_method = tdb->find_method("app.AlbumManager", "saveCapturePhoto");
    save_capture_method->add_hook(pre_save_capture_photo_hook, post_save_capture_photo_hook, false);

    auto quest_result_start_method = api->tdb()->find_method("app.GUIFlowQuestResult.cContext", "onStartFlow");
    quest_result_start_method->add_hook(pre_open_quest_result_ui, null_post, false);

    auto quest_result_end_method = api->tdb()->find_method("app.GUIFlowQuestResult.cContext", "onEndFlow");
    quest_result_end_method->add_hook(pre_close_quest_result_ui, null_post, false);

    quest_success_force_client = std::make_unique<FileInjectClient>();
    quest_failure_force_client = std::make_unique<FileInjectClient>();
    quest_cancel_force_client = std::make_unique<FileInjectClient>();
    null_capture_client = std::make_unique<NullCaptureInjectClient>();

    quest_success_force_client->set_limit_size(false);
    quest_failure_force_client->set_limit_size(false);
    quest_cancel_force_client->set_limit_size(false);
}

void Plugin_QuestResult::initialize(const REFrameworkPluginInitializeParam *params) {
    static const char *SETTINGS_NAME = "mhwilds_high_quality_photos";
    static const char *DEBUG_FILE_POSTFIX = "QuestResult";

    plugin_instance = std::make_unique<Plugin_QuestResult>(params);
    PluginBase::base_initialize(plugin_instance.get(), params, SETTINGS_NAME, DEBUG_FILE_POSTFIX);

    auto& api = reframework::API::get();

    GameUIController::initialize(params);
    ReShadeAddOnInjectClient::initialize();
    CaptureResolutionInject::initialize(api.get());
    GameProducedMaxQualityInjectClient::initialize(api.get());
}

Plugin_QuestResult *Plugin_QuestResult::get_instance() {
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
    Plugin_QuestResult::initialize(param);

    return true;
}
