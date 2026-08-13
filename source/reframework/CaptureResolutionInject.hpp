#pragma once

#include <reframework/API.hpp>
#include <map>
#include <tuple>

class CaptureResolutionInject {
public:
    using Resolution = std::pair<int, int>;

private:
    struct ResourceInfo {
        reframework::API::Resource *resource = nullptr;
        reframework::API::ManagedObject *holder = nullptr;
        bool is_plugin_managed = true;
    };

    using RenderTargetMap = std::map<Resolution, ResourceInfo>;

    RenderTargetMap capture_render_targets_by_resolution_16x9;
    RenderTargetMap capture_render_targets_by_resolution_21x9;

    reframework::API* api = nullptr;
    reframework::API::ManagedObject *album_manager = nullptr;
    reframework::API::Method *get_main_view_method = nullptr;
    reframework::API::Method *get_size_method = nullptr;
    reframework::API::Method *is_widescreen_method = nullptr;

    reframework::API::ManagedObject **render_target_texture_holder_field_16x9 = nullptr;
    reframework::API::ManagedObject **render_target_texture_holder_field_21x9 = nullptr;

    reframework::API::ManagedObject *default_16x9_render_target_texture_holder = nullptr;
    reframework::API::ManagedObject *default_21x9_render_target_texture_holder = nullptr;

    reframework::API::ManagedObject *gui_quest_instance = nullptr;
    reframework::API::Method *set_texture_method = nullptr;

    bool is_gui_quest_closed = false;

    Resolution current_resolution_16x9;
    Resolution current_resolution_21x9;

    bool enabled = false;

    reframework::API::ManagedObject *find_nearest_render_target_texture_holder(float current_width, float current_height, RenderTargetMap& render_target_map,
        Resolution *output_resolution) const;

    void update_gui_texture();

    // Lazily resolves app.AlbumManager and the render target fields/holders on first use.
    bool ensure_album_manager_and_fields();

    // Queries the current game resolution via via.SceneManager (mirrors the REFramework Lua GetScreenSize()).
    bool get_screen_size(float& width, float& height);

public:
    static int pre_gui070002_on_open(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_gui070002_on_open(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

    static int pre_gui070002_on_close(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
    static void post_gui070002_on_close(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

public:
    explicit CaptureResolutionInject(reframework::API* api_instance);

    void update_resolution();
    void revert();
    void cleanup();

    static void initialize(reframework::API* api_instance);
    static CaptureResolutionInject* get_instance();

    Resolution get_current_resolution_16x9() const {
        return current_resolution_16x9;
    }

    Resolution get_current_resolution_21x9() const {
        return current_resolution_21x9;
    }
};