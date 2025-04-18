#include "GameUIController.hpp"
#include "reframework/API.hpp"
#include <memory>

using namespace reframework;

std::unique_ptr<GameUIController> game_ui_controller_instance = nullptr;

bool game_ui_controller_on_pre_gui_draw_element(void*, void*) {
    if (game_ui_controller_instance == nullptr) {
        return true;
    }

    // Return true to draw the UI
    return !game_ui_controller_instance->is_in_hide();
}

GameUIController::GameUIController(const REFrameworkPluginInitializeParam* initialize_params) {
    if (initialize_params != nullptr) {
        initialize_params->functions->on_pre_gui_draw_element(game_ui_controller_on_pre_gui_draw_element);
    }
}

void GameUIController::hide_for(int frame_count) {
    if (is_in_hide()) {
        API::get()->log_info("Already hiding, ignoring request to hide for %d frames", frame_count);
        return;
    } else {
        hide_frames_passed = 0;
        total_hide_frame = frame_count;
    }
}

void GameUIController::show_impl() {
    if (is_in_hide()) {
        hide_frames_passed = -1;
    } else {
        API::get()->log_info("Not hiding, ignoring request to show");
    }
}

float GameUIController::get_hiding_progress() const {
    if (!is_in_hide() || is_hiding_until_notice()) {
        return 0.0f;
    }

    return static_cast<float>(hide_frames_passed) / static_cast<float>(total_hide_frame);
}

GameUIController *GameUIController::get_instance() {
    return game_ui_controller_instance ? game_ui_controller_instance.get() : nullptr;
}

void GameUIController::initialize(const REFrameworkPluginInitializeParam *initialize_params) {
    if (game_ui_controller_instance == nullptr) {
        game_ui_controller_instance = std::unique_ptr<GameUIController>(new GameUIController(initialize_params));
    }
}

void GameUIController::update() {
    if (is_in_hide()) {
        if (!is_hiding_until_notice()) {
            hide_frames_passed++;
        
            if (hide_frames_passed > total_hide_frame) {
                show_impl();
            }
        }
    }
}