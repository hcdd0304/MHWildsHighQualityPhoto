#pragma once

typedef void (*ScreenCaptureFinishFunc)(int result, int width, int height, void *data);

const int RESULT_SCREEN_CAPTURE_IN_PROGRESS = -2;
const int RESULT_SCREEN_RESHADE_CAPTURE_FAILURE = -1;
const int RESULT_SCREEN_CAPTURE_NO_RESHADE_RUNTIME = -3;
const int RESULT_SCREEN_CAPTURE_HDR_NOT_SAVEABLE = -4;
const int RESULT_SCREEN_CAPTURE_HDR_TO_SDR_FAILED = -5;
const int RESULT_SCREEN_CAPTURE_HDR_FAILED = -6;
const int RESULT_SCREEN_CAPTURE_SUCCESS = 0;
const int RESULT_SCREEN_CAPTURE_SUBMITTED = 1;
const int RESULT_SCREEN_CAPTURE_DATA_DOWNLOADED = 2;

extern "C" __declspec(dllexport) int request_screen_capture(ScreenCaptureFinishFunc finish_callback, int hdr_bit_depths, bool screenshot_before_reshade);
extern "C" __declspec(dllexport) void set_reshade_filters_enable(bool should_enable);