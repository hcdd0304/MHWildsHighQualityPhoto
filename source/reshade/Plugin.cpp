#include <reshade.hpp>
#include <sk_hdr_png.hpp>
#include <filesystem>
#include <format>
#include <subprocess.h>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_image_write_hdr_png.h>
#include <fstream>
#include <thread>
#include <vector>

#include "Plugin.h"
#include "HDRProcessing.hpp"
#include "JXLDef.hpp"
 
extern "C" __declspec(dllexport) const char *NAME = "High Quality Kill Screen Capturer";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Take a screenshot when you finish a quest, then send it to the game for displaying";

reshade::api::effect_runtime *current_reshade_runtime = nullptr;
reshade::api::swapchain *current_swapchain = nullptr;

int g_hdr_bit_depths = 11; // Default to 11-bit depth for HDR
ScreenCaptureFinishFunc g_finish_callback = nullptr;
bool screenshot_requested = false;

std::atomic_bool is_hdr_converting = false;
bool g_screenshot_before_reshade = false;

std::vector<std::uint8_t> g_cached_pixels;
std::vector<std::uint8_t> g_cached_converted_pixels;

struct ReshadeVersion {
    int major;
    int minor;
    int patch;
    int revision;
} version_info;

static void cache_reshade_version(const char *reshade_version) {
    if (reshade_version == nullptr) {
        reshade::log::message(reshade::log::level::error, "ReShadeVersion is null");
        return;
    }

    int major = 0, minor = 0, patch = 0, revision = 0;
    if (sscanf_s(reshade_version, "%d.%d.%d.%d", &major, &minor, &patch, &revision) < 3) {
        auto msg = std::format("Failed to parse ReShade version string: {}", reshade_version);
        reshade::log::message(reshade::log::level::error, msg.c_str());
        return;
    }

    version_info.major = major;
    version_info.minor = minor;
    version_info.patch = patch;
    version_info.revision = revision;

    auto msg = std::format("Parsed ReShade version string: {}", reshade_version);
    reshade::log::message(reshade::log::level::debug, msg.c_str());
}

std::string get_current_dll_path() {
    char path[MAX_PATH];
    HMODULE hm = NULL;

    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR) &request_screen_capture, &hm) == 0)
    {
        int ret = GetLastError();
        fprintf(stderr, "GetModuleHandle failed, error = %d\n", ret);
        // Return or however you want to handle an error.
    }
    if (GetModuleFileName(hm, path, sizeof(path)) == 0)
    {
        int ret = GetLastError();
        fprintf(stderr, "GetModuleFileName failed, error = %d\n", ret);
        // Return or however you want to handle an error.
    }

    return std::string(path);
}

static void convert_hdr_to_sdr_with_hdrfix(const std::string &input_path, std::chrono::system_clock::rep unique_id) {
    // Call hdrfix to convert HDR to SDR
    auto original_dll_containing_path = std::filesystem::path(get_current_dll_path()).parent_path().parent_path();

    if (!std::filesystem::exists(input_path)) {
        auto msg = std::format("Input HDR file does not exist: {}", input_path);
        reshade::log::message(reshade::log::level::warning, msg.c_str());
    }

    auto path_to_exe = (original_dll_containing_path / "hdrfix.exe").string();
    auto path_output = (std::filesystem::temp_directory_path() / std::format("hdrfix_output{0}.png", unique_id)).string();
    const char *command_line[] = { path_to_exe.c_str(), input_path.c_str(), path_output.c_str(), nullptr };

    struct subprocess_s subprocess;
    int result = subprocess_create(command_line, subprocess_option_no_window, &subprocess);

    if (0 != result) {
        if (!std::filesystem::exists(path_to_exe)) {
            reshade::log::message(reshade::log::level::error, "hdrfix.exe not found, cannot convert HDR screenshot to SDR");
        }

        auto msg = std::format("Failed to launch hdrfix.exe, subprocess_create returned error code {}", result);
        reshade::log::message(reshade::log::level::error, msg.c_str());

        if (g_finish_callback) {
            g_finish_callback(RESULT_SCREEN_CAPTURE_HDR_TO_SDR_FAILED, 0, 0, nullptr);
        }

        g_finish_callback = nullptr;
        return;
    }

    int process_return_code = 0;
    subprocess_join(&subprocess, &process_return_code);

#ifdef LOG_DEBUG_STEP
    reshade::log::message(reshade::log::level::debug, "HDR convert finished");
#endif

    if (0 != process_return_code) {
        auto msg = std::format("hdrfix.exe failed with return code {}", process_return_code);
        reshade::log::message(reshade::log::level::warning, msg.c_str());

        /*
        if (g_finish_callback) {
            g_finish_callback(RESULT_SCREEN_CAPTURE_HDR_TO_SDR_FAILED, 0, 0, nullptr);
        }
        g_finish_callback = nullptr;
        return;
        */
    }

    if (!std::filesystem::exists(path_output)) {
        auto msg = std::format("hdrfix.exe did not produce output file at expected location: {}", path_output);
        reshade::log::message(reshade::log::level::error, msg.c_str());

        if (g_finish_callback) {
            g_finish_callback(RESULT_SCREEN_CAPTURE_HDR_TO_SDR_FAILED, 0, 0, nullptr);
        }
        g_finish_callback = nullptr;
        return;
    }

    // Read the output file and extract it
    auto stream = std::ifstream(path_output, std::ios::binary | std::ios::ate);
    auto size = stream.tellg();

    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(size);

    stream.read(reinterpret_cast<char*>(data.data()), size);
    stream.close();

    int loaded_width, loaded_height;
    int comp = 4;
    stbi_uc *data_result = stbi_load_from_memory(data.data(), static_cast<int>(size), &loaded_width, &loaded_height, &comp, 4);

    if (data_result) {
#ifdef LOG_DEBUG_STEP
        reshade::log::message(reshade::log::level::debug, "Reading HDR convert OK, sending to callback");
#endif

        if (g_finish_callback) {
            g_finish_callback(RESULT_SCREEN_CAPTURE_SUCCESS, loaded_width, loaded_height, data_result);
        }
        stbi_image_free(data_result);
    } else {
        reshade::log::message(reshade::log::level::error, "Failed to read converted SDR image from hdrfix output");

        if (g_finish_callback) {
            g_finish_callback(RESULT_SCREEN_CAPTURE_HDR_NOT_SAVEABLE, 0, 0, nullptr);
        }
    }

    g_finish_callback = nullptr;
}

static void hdr_convert_thread(std::uint8_t * pixels, std::uint32_t width, std::uint32_t height, reshade::api::format format, int hdr_bit_depths) {
    // Launch this in a separate thread
    // Write to PNG and use tool to convert to SDR
    auto unique_id = std::chrono::system_clock::now().time_since_epoch().count();
    auto temp_path = std::filesystem::temp_directory_path() / std::format("reshade_hdr_temporary_screenshot{0}.png", unique_id);
    auto temp_path_string = temp_path.string();
    auto temp_path_wstring = temp_path.wstring();

    auto original_dll_containing_path = std::filesystem::path(get_current_dll_path()).parent_path().parent_path();
    auto current_directory_output_path = original_dll_containing_path / std::format("reshade_hdr_screenshot{0}.png", unique_id);
    
#ifdef LOG_DEBUG_STEP
    reshade::log::message(reshade::log::level::debug, "Write HDR screenshot to disk");
#endif

    if (!sk_hdr_png::write_image_to_disk(temp_path_wstring.c_str(),
        width, height,
        reinterpret_cast<void*>(pixels),
        g_hdr_bit_depths,
        format)) {
        if (g_finish_callback) {
            g_finish_callback(RESULT_SCREEN_CAPTURE_HDR_NOT_SAVEABLE, 0, 0, nullptr);
        }
        g_finish_callback = nullptr; // Reset the callback to allow new requests
        is_hdr_converting = false;
        return;
    }

#ifdef LOG_DEBUG_STEP
    reshade::log::message(reshade::log::level::debug, "Call HDR fix to convert to SDR");
#endif

    // Copy temp_path file to current directory for hdrfix to work around its path issues
    std::filesystem::copy_file(temp_path, current_directory_output_path, std::filesystem::copy_options::overwrite_existing);

    auto msg_copy = std::format("Copied HDR temp file to current directory for hdrfix: {}", current_directory_output_path.string());
    reshade::log::message(reshade::log::level::debug, msg_copy.c_str());

    // Use shared function to convert HDR to SDR
    convert_hdr_to_sdr_with_hdrfix(temp_path_string, unique_id);
    is_hdr_converting = false;
}

static void hdr_save_thread_v67(std::uint8_t *pixels, std::uint32_t width, std::uint32_t height, reshade::api::format quantization_format, reshade::api::color_space color_space) {
    // New HDR processing for ReShade 6.7+ - saves directly to PNG with proper color space handling
    auto unique_id = std::chrono::system_clock::now().time_since_epoch().count();
    auto temp_path = std::filesystem::temp_directory_path() / std::format("reshade_hdr_screenshot{0}.png", unique_id);
    auto temp_path_string = temp_path.string();

#ifdef LOG_DEBUG_STEP
    reshade::log::message(reshade::log::level::debug, "Saving HDR screenshot to PNG (v6.7+)");
#endif

    // Post-process HDR pixels if needed (color space conversion, tone mapping, etc.)
    HDRProcessing::post_process_hdr(pixels, width, height, quantization_format);

    bool save_success = false;

    if (FILE *const file = _wfsopen(std::wstring(temp_path_string.begin(), temp_path_string.end()).c_str(), L"wb", SH_DENYNO))
    {
        const auto write_callback = [](void *context, void *data, int size) {
            fwrite(data, 1, size, static_cast<FILE *>(context));
        };

        int comp = 3; // RGB for HDR (no alpha)

        // Handle HDR PNG writing with proper color space
        save_success = stbi_write_hdr_png_to_func(
            write_callback,
            file,
            width,
            height,
            comp,
            reinterpret_cast<uint16_t *>(pixels),
            0,
            static_cast<unsigned char>(JXL_PRIMARIES_2100),
            static_cast<unsigned char>(color_space == reshade::api::color_space::hdr10_hlg ? JXL_TRANSFER_FUNCTION_HLG : JXL_TRANSFER_FUNCTION_PQ)) != 0;

        if (ferror(file))
            save_success = false;

        fclose(file);
    }

    if (save_success) {
#ifdef LOG_DEBUG_STEP
        reshade::log::message(reshade::log::level::debug, "HDR PNG save successful, converting to SDR");
#endif

        // Use shared function to convert HDR to SDR
        convert_hdr_to_sdr_with_hdrfix(temp_path_string, unique_id);
    } else {
#ifdef LOG_DEBUG_STEP
        reshade::log::message(reshade::log::level::debug, "HDR PNG save failed");
#endif

        if (g_finish_callback) {
            g_finish_callback(RESULT_SCREEN_CAPTURE_HDR_NOT_SAVEABLE, 0, 0, nullptr);
        }
        g_finish_callback = nullptr;
    }

    is_hdr_converting = false;
}

static bool is_screenshot_requested() {
    return screenshot_requested;
}

static void capture_screenshot_impl();

static void on_present_without_effects_applied(reshade::api::command_queue *queue, reshade::api::swapchain *swapchain, const reshade::api::rect *source_rect,
    const reshade::api::rect *dest_rect, uint32_t dirty_rect_count, const reshade::api::rect *dirty_rect) {
    current_swapchain = swapchain;

    if (is_screenshot_requested()) {
        if (g_screenshot_before_reshade) {
            capture_screenshot_impl();
        }
    }
}

static void on_present_with_effects_applied(reshade::api::effect_runtime *runtime)
{
    current_reshade_runtime = runtime;

    if (is_screenshot_requested()) {
        if (!g_screenshot_before_reshade) {
            capture_screenshot_impl();
        }
    }
}

// Copied
const std::uint8_t *do_quanitization(reshade::api::format quantization_format, reshade::api::format source_format, std::vector<std::uint8_t> &pixels_vector, const std::uint8_t *mapped_pixels, int width, int height) {
    if (quantization_format == source_format) {
        return mapped_pixels;
    }
    
    const uint32_t pixels_row_pitch = reshade::api::format_row_pitch(quantization_format, width);
    const uint32_t mapped_pixels_row_pitch = reshade::api::format_row_pitch(source_format, width);

    auto result_size = static_cast<std::size_t>(height) * pixels_row_pitch;
    if (pixels_vector.size() < result_size) {
        pixels_vector.resize(result_size);
    }

    auto pixels = pixels_vector.data();

    for (size_t y = 0; y < height; ++y, pixels += pixels_row_pitch, mapped_pixels += mapped_pixels_row_pitch) {
        if (quantization_format == reshade::api::format::r8g8b8a8_unorm)
        {
            switch (source_format)
            {
            case reshade::api::format::r8_unorm:
                for (size_t x = 0; x < width; ++x)
                {
                    pixels[x * 4 + 0] = mapped_pixels[x];
                    pixels[x * 4 + 1] = 0;
                    pixels[x * 4 + 2] = 0;
                    pixels[x * 4 + 3] = 0xFF;
                }
                continue;
            case reshade::api::format::r8g8_unorm:
                for (size_t x = 0; x < width; ++x)
                {
                    pixels[x * 4 + 0] = mapped_pixels[x * 2 + 0];
                    pixels[x * 4 + 1] = mapped_pixels[x * 2 + 1];
                    pixels[x * 4 + 2] = 0;
                    pixels[x * 4 + 3] = 0xFF;
                }
                continue;
            case reshade::api::format::r8g8b8x8_unorm:
                for (size_t x = 0; x < pixels_row_pitch; x += 4)
                {
                    pixels[x + 0] = mapped_pixels[x + 0];
                    pixels[x + 1] = mapped_pixels[x + 1];
                    pixels[x + 2] = mapped_pixels[x + 2];
                    pixels[x + 3] = 0xFF;
                }
                continue;
            case reshade::api::format::b8g8r8a8_unorm:
                // Format is BGRA, but output should be RGBA, so flip channels
                for (size_t x = 0; x < pixels_row_pitch; x += 4)
                {
                    pixels[x + 0] = mapped_pixels[x + 2];
                    pixels[x + 1] = mapped_pixels[x + 1];
                    pixels[x + 2] = mapped_pixels[x + 0];
                    pixels[x + 3] = mapped_pixels[x + 3];
                }
                continue;
            case reshade::api::format::b8g8r8x8_unorm:
                for (size_t x = 0; x < pixels_row_pitch; x += 4)
                {
                    pixels[x + 0] = mapped_pixels[x + 2];
                    pixels[x + 1] = mapped_pixels[x + 1];
                    pixels[x + 2] = mapped_pixels[x + 0];
                    pixels[x + 3] = 0xFF;
                }
                continue;
            case reshade::api::format::r10g10b10a2_unorm:
            case reshade::api::format::b10g10r10a2_unorm:
                for (size_t x = 0; x < pixels_row_pitch; x += 4)
                {
                    const auto offset_r = source_format == reshade::api::format::b10g10r10a2_unorm ? 2 : 0;
                    const auto offset_g = 1;
                    const auto offset_b = source_format == reshade::api::format::b10g10r10a2_unorm ? 0 : 2;
                    const auto offset_a = 3;

                    const uint32_t rgba = *reinterpret_cast<const uint32_t *>(mapped_pixels + x);
                    // Divide by 4 to get 10-bit range (0-1023) into 8-bit range (0-255)
                    pixels[x + offset_r] = (( rgba & 0x000003FFu)        /  4) & 0xFF;
                    pixels[x + offset_g] = (((rgba & 0x000FFC00u) >> 10) /  4) & 0xFF;
                    pixels[x + offset_b] = (((rgba & 0x3FF00000u) >> 20) /  4) & 0xFF;
                    pixels[x + offset_a] = (((rgba & 0xC0000000u) >> 30) * 85) & 0xFF;
                }
                continue;
            }
        }
        else if (quantization_format == reshade::api::format::r16g16b16_unorm)
        {
            switch (source_format)
            {
            case reshade::api::format::r10g10b10a2_unorm:
            case reshade::api::format::b10g10r10a2_unorm:
                for (size_t x = 0; x < pixels_row_pitch; x += sizeof(uint16_t) * 3)
                {
                    const auto offset_r = source_format == reshade::api::format::b10g10r10a2_unorm ? 2 : 0;
                    const auto offset_g = 1;
                    const auto offset_b = source_format == reshade::api::format::b10g10r10a2_unorm ? 0 : 2;

                    const uint32_t rgba = *reinterpret_cast<const uint32_t *>(mapped_pixels + (x / (sizeof(uint16_t) * 3)) * 4);
                    // Multiply by 64 to get 10-bit range (0-1023) into 16-bit range (0-65535)
                    reinterpret_cast<uint16_t *>(pixels + x)[offset_r] = ( (rgba & 0x000003FFu)        * 64) & 0xFFFF;
                    reinterpret_cast<uint16_t *>(pixels + x)[offset_g] = (((rgba & 0x000FFC00u) >> 10) * 64) & 0xFFFF;
                    reinterpret_cast<uint16_t *>(pixels + x)[offset_b] = (((rgba & 0x3FF00000u) >> 20) * 64) & 0xFFFF;
                }
                continue;
            }
        }
        else if (quantization_format == reshade::api::format::r16g16b16_float && source_format == reshade::api::format::r16g16b16a16_float)
        {
            for (size_t x = 0; x < pixels_row_pitch; x += sizeof(uint16_t) * 3)
            {
                std::memcpy(pixels + x, mapped_pixels + (x / 3) * 4, sizeof(uint16_t) * 3);
            }
            continue;
        }
        else if (quantization_format == reshade::api::format::r10g10b10a2_unorm && source_format == reshade::api::format::b10g10r10a2_unorm)
        {
            // Format is BGRA, but output should be RGBA, so flip channels
            for (size_t x = 0; x < pixels_row_pitch; x += sizeof(uint32_t))
            {
                const uint32_t rgba = *reinterpret_cast<const uint32_t *>(mapped_pixels + x);
                *reinterpret_cast<uint32_t *>(pixels + x) = ((rgba & 0x000003FFu) << 20) | ((rgba & 0x3FF00000u) >> 20) | (rgba & 0xC00FFC00u);
            }
            continue;
        }
    }

    return pixels_vector.data();
}

static void capture_screenshot_impl() {
    if (!is_screenshot_requested()) {
        return;
    }

    if (is_hdr_converting) {
        return;
    }

    screenshot_requested = false;

    auto back_buffer = current_reshade_runtime->get_current_back_buffer();
    auto resource_description = current_reshade_runtime->get_device()->get_resource_desc(back_buffer);

    auto format = reshade::api::format_to_default_typed(resource_description.texture.format);
    auto color_space = current_swapchain->get_color_space();

    if (version_info.major >= 6 && version_info.minor >= 7) {
        // From ReShade 6.7 onwards, the pixels have to be manually quantized
        bool is_hdr = (format == reshade::api::format::r16g16b16a16_float) || (color_space == reshade::api::color_space::hdr10_st2084) ||
                      (color_space == reshade::api::color_space::hdr10_hlg);

        std::uint32_t width, height;
        current_reshade_runtime->get_screenshot_width_and_height(&width, &height);
        
        auto bytes_per_pixel = (format == reshade::api::format::r16g16b16a16_float) ? 8 : 4; // 4 bytes for RGBA, 8 bytes for HDR scRGB
        std::size_t required_size = width * height * bytes_per_pixel;
        
        // Reuse cached buffer or reallocate if needed
        if (g_cached_pixels.size() < required_size) {
            g_cached_pixels.resize(required_size);
        }
        
        auto pixels = g_cached_pixels.data();

    #ifdef LOG_DEBUG_STEP
        reshade::log::message(reshade::log::level::debug, "Start capturing screenshot (v6.7+)");
    #endif

        if (!current_reshade_runtime->capture_screenshot(pixels)) {
            if (g_finish_callback) {
                g_finish_callback(RESULT_SCREEN_RESHADE_CAPTURE_FAILURE, 0, 0, nullptr);
            }
            g_finish_callback = nullptr; // Reset the callback to allow new requests
            return;
        }

        auto quantization_format = reshade::api::format::r8g8b8a8_unorm;

        if (is_hdr) {
            if (format == reshade::api::format::r16g16b16a16_float) {
                quantization_format = reshade::api::format::r16g16b16_float;
            } else {
                quantization_format = reshade::api::format::r16g16b16_unorm;
            }
        }

        // Perform quantization
        const auto quantized_pixels = do_quanitization(quantization_format, format, g_cached_converted_pixels, pixels, width, height);

    #ifdef LOG_DEBUG_STEP
        reshade::log::message(reshade::log::level::debug, "Capturing screenshot finished");
    #endif

        if (g_finish_callback != nullptr) {
            g_finish_callback(RESULT_SCREEN_CAPTURE_DATA_DOWNLOADED, width, height, nullptr);
        }

        // Create a thread that dump qnauntized pixels to PNG
        /*
        auto dump_to_png_quantized = [=]() {
            auto original_dll_containing_path = std::filesystem::path(get_current_dll_path()).parent_path().parent_path();
            auto path_output = (original_dll_containing_path / "test.png").string();

            stbi_write_png_to_func(
                [](void *context, void *data, int size) {
                    fwrite(data, 1, size, static_cast<FILE *>(context));
                },
                fopen(path_output.c_str(), "wb"),
                width,
                height,
                (quantization_format == reshade::api::format::r8g8b8a8_unorm) ? 4 : 3,
                quantized_pixels,
                0);

                reshade::log::message(reshade::log::level::debug, "Dumped quantized PNG for debugging to test.png");
        };

        std::thread(dump_to_png_quantized).detach();*/

        if (!is_hdr) {
    #ifdef LOG_DEBUG_STEP
            reshade::log::message(reshade::log::level::debug, "Screenshot is not HDR, sending it directly");
    #endif

            if (g_finish_callback) {
                g_finish_callback(RESULT_SCREEN_CAPTURE_SUCCESS, width, height, const_cast<std::uint8_t*>(quantized_pixels));
            }

            g_finish_callback = nullptr; // Reset the callback to allow new requests
            return;
        } else {
    #ifdef LOG_DEBUG_STEP
            reshade::log::message(reshade::log::level::debug, "Screenshot is HDR, launching HDR PNG save");
    #endif

            // Launch a thread to save the HDR image directly to PNG with proper color space
            is_hdr_converting = true;
            std::thread(hdr_save_thread_v67, const_cast<std::uint8_t*>(quantized_pixels), width, height, quantization_format, color_space).detach();
            return;
        }
    }
    else {
        bool is_hdr = (format == reshade::api::format::r16g16b16a16_float) ||
                    ((format == reshade::api::format::b10g10r10a2_unorm) || (format == reshade::api::format::r10g10b10a2_unorm)) && (color_space == reshade::api::color_space::hdr10_st2084);

        std::uint32_t width, height;
        current_reshade_runtime->get_screenshot_width_and_height(&width, &height);
        
        auto bytes_per_pixel = (format == reshade::api::format::r16g16b16a16_float) ? 8 : 4; // 4 bytes for RGBA, 8 bytes for HDR scRGB
        std::size_t required_size = width * height * bytes_per_pixel;
        
        // Reuse cached buffer or reallocate if needed
        if (g_cached_pixels.size() < required_size) {
            g_cached_pixels.resize(required_size);
        }
        
        auto pixels = g_cached_pixels.data();

    #ifdef LOG_DEBUG_STEP
        reshade::log::message(reshade::log::level::debug, "Start capturing screenshot (v6.7-)");
    #endif

        if (!current_reshade_runtime->capture_screenshot(pixels)) {
            if (g_finish_callback) {
                g_finish_callback(RESULT_SCREEN_RESHADE_CAPTURE_FAILURE, 0, 0, nullptr);
            }
            g_finish_callback = nullptr; // Reset the callback to allow new requests
            return;
        }

    #ifdef LOG_DEBUG_STEP
        reshade::log::message(reshade::log::level::debug, "Capturing screenshot finished");
    #endif

        if (g_finish_callback != nullptr) {
            g_finish_callback(RESULT_SCREEN_CAPTURE_DATA_DOWNLOADED, width, height, nullptr);
        }

        if (!is_hdr) {
    #ifdef LOG_DEBUG_STEP
            reshade::log::message(reshade::log::level::debug, "Screenshot is not HDR, sending it directly");
    #endif

            if (g_finish_callback) {
                g_finish_callback(RESULT_SCREEN_CAPTURE_SUCCESS, width, height, pixels);
            }
            g_finish_callback = nullptr; // Reset the callback to allow new requests
            return;
        } else {
    #ifdef LOG_DEBUG_STEP
            reshade::log::message(reshade::log::level::debug, "Screenshot is HDR, launching conversion to SDR");
    #endif

            // Launch a thread to convert the HDR image to SDR
            is_hdr_converting = true;
            std::thread(hdr_convert_thread, pixels, width, height, format, g_hdr_bit_depths).detach();
            return;
        }
    }
}

extern "C" int request_screen_capture(ScreenCaptureFinishFunc finish_callback, int hdr_bit_depths, bool screenshot_before_reshade) {
    if (g_finish_callback) {
        return RESULT_SCREEN_CAPTURE_IN_PROGRESS;
    }

    g_finish_callback = finish_callback;
    g_hdr_bit_depths = hdr_bit_depths;
    g_screenshot_before_reshade = screenshot_before_reshade;
    screenshot_requested = true;

    return RESULT_SCREEN_CAPTURE_SUBMITTED;
}

extern "C" void set_reshade_filters_enable(bool should_enable) {
    current_reshade_runtime->set_effects_state(should_enable);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH: {
        // Call 'reshade::register_addon()' before you call any other function of the ReShade API.
        // This will look for the ReShade instance in the current process and initialize the API when found.
        if (!reshade::register_addon(hinstDLL))
            return FALSE;

        auto reshade_module = reshade::internal::get_reshade_module_handle();

        if (reshade_module == nullptr) {
            reshade::log::message(reshade::log::level::error, "Failed to get ReShade module handle during DLL_PROCESS_ATTACH");
        }
        else {
            auto reshade_str_ptr = reinterpret_cast<const char **>(GetProcAddress(reshade_module, "ReShadeVersion"));
            if (reshade_str_ptr == nullptr) {
                reshade::log::message(reshade::log::level::error, "Failed to get ReShadeVersion address during DLL_PROCESS_ATTACH");
            }
            else {
                const char *reshade_version = *reshade_str_ptr;
                cache_reshade_version(reshade_version);
            }
        }

        // This registers a callback for the 'present' event, which occurs every time a new frame is presented to the screen.
        // The function signature has to match the type defined by 'reshade::addon_event_traits<reshade::addon_event::present>::decl'.
        // For more details check the inline documentation for each event in 'reshade_events.hpp'.
        reshade::register_event<reshade::addon_event::present>(&on_present_without_effects_applied);
        reshade::register_event<reshade::addon_event::reshade_present>(&on_present_with_effects_applied);
        break;
    }
    case DLL_PROCESS_DETACH:
        // Optionally unregister the event callback that was previously registered during process attachment again.
        reshade::unregister_event<reshade::addon_event::present>(&on_present_without_effects_applied);
        reshade::unregister_event<reshade::addon_event::reshade_present>(&on_present_with_effects_applied);
        // And finally unregister the add-on from ReShade (this will automatically unregister any events and overlays registered by this add-on too).
        reshade::unregister_addon(hinstDLL);
        break;
    }
    return TRUE;
}