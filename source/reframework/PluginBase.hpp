#pragma once

#include <reframework/API.hpp>

struct ModSettings;

class PluginBase {
private:
    void open_blocking_webp_pick_file_dialog(std::string &file_path);

protected:
    virtual void draw_user_interface() = 0;
    virtual void update() = 0;
    virtual void late_update() = 0;
    virtual void end_rendering() = 0;

    void draw_user_interface_path(const std::string &label, std::string &target_path, bool limit_size);

    static void base_initialize(PluginBase *plugin, const REFrameworkPluginInitializeParam *params,
        std::string_view settings_name, std::string_view debug_file_postfix);

    static bool is_override_image_path_valid(const std::string& path, bool limit_size);

public:
    explicit PluginBase() = default;
    ~PluginBase() = default;
};