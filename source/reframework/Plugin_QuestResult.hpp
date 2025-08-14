#pragma once

#include <reframework/API.hpp>
#include <memory>

#include "InjectClient/FileInjectClient.hpp"
#include "InjectClient/NullInjectClient.hpp"
#include "WebPCaptureInjectClient.hpp"
#include "PluginBase.hpp"

struct ModSettings;

class Plugin_QuestResult : PluginBase {
private:
    std::unique_ptr<FileInjectClient> quest_success_force_client;
    std::unique_ptr<FileInjectClient> quest_failure_force_client;
    std::unique_ptr<FileInjectClient> quest_cancel_force_client;
    std::unique_ptr<NullCaptureInjectClient> null_capture_client;

    void decide_and_set_screen_cap_or_override_inject(std::unique_ptr<FileInjectClient> &override_client,
        bool should_override,
        const std::string &override_path,
        bool should_lossless,
        bool is_photo_mode);

protected:
    void draw_user_interface() override;
    void update() override;
    void late_update() override;
    void end_rendering() override;

private:
    // Hooks
    static int pre_quest_failure_hook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_quest_failure_hook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_quest_success_hook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_quest_success_hook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_quest_cancel_hook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_quest_cancel_hook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_save_capture_photo_hook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_save_capture_photo_hook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_open_quest_result_ui(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static int pre_close_quest_result_ui(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void null_post(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

public:
    explicit Plugin_QuestResult(const REFrameworkPluginInitializeParam *params);
    ~Plugin_QuestResult() = default;

    static void initialize(const REFrameworkPluginInitializeParam *params);
    static Plugin_QuestResult *get_instance();
};