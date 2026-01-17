#pragma once

#include <cstdint>

enum SaveCaptureState {
    SAVECAPTURESTATE_IDLE = 0,
    SAVECAPTURESTATE_WAIT_UI_OFF = 1,
    SAVECAPTURESTATE_START = 2,
    SAVECAPTURESTATE_COPY_TO_STAGING = 3,
    SAVECAPTURESTATE_WAIT_STAGING = 4,
    SAVECAPTURESTATE_SERIALIZE = 5,
    SAVECAPTURESTATE_WAIT_SERIALIZE = 6,
    SAVECAPTURESTATE_SAVE_CAPTURE = 7,
    SAVECAPTURESTATE_WAIT_SAVE_CAPTURE = 8,
    SAVECAPTURESTATE_WAIT_SAVE_CAPTURE_FOR_LINKED_SAVE = 9,
    SAVECAPTURESTATE_AFTER_SAVE = 10,
};

enum LoadCaptureState {
    LOADCAPTURESTATE_DESERIALIZE = 4
};

// See via.render.SerializedResult.get_Content
const std::size_t SerializeResultContentArrayOffset = 0x90;
const std::size_t MaxSerializePhotoSize = 0xF0000;
const std::size_t MaxSerializePhotoSizeOriginal = 0x40000;