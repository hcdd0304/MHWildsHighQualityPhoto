#pragma once

#include <cstdint>
#include <immintrin.h>
#include <cmath>
#include <reshade.hpp>

namespace HDRProcessing {

/**
 * Convert 16-bit floating point HDR values to PQ-encoded 16-bit unsigned integers
 * Handles color space conversion from BT.709/sRGB to BT.2020 and applies PQ tone mapping
 * 
 * @param pixels Pointer to pixel data (r16g16b16_float format, 3 uint16 per pixel)
 * @param width Image width in pixels
 * @param height Image height in pixels
 */
inline void convert_float16_to_pq_uint16(std::uint8_t *pixels, std::uint32_t width, std::uint32_t height) {
    for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i)
    {
        uint16_t *const pixel = reinterpret_cast<uint16_t *>(pixels) + i * 3;
        alignas(16) uint16_t result[4] = { pixel[0], pixel[1], pixel[2] };

        // Convert 16-bit floating point values to 32-bit floating point
        auto rgba_float_srgb = _mm_cvtph_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(result)));

        // Convert BT.709/sRGB to BT.2020 primaries
        // These constants represent the color matrix transformation from sRGB to BT.2020
        auto rgba_float_bt2100 = _mm_max_ps(_mm_setzero_ps(),
            _mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(rgba_float_srgb, rgba_float_srgb, 0b00000000), _mm_setr_ps(0.627403914928436279296875f,     0.069097287952899932861328125f,    0.01639143936336040496826171875f, 0.0f)),
            _mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(rgba_float_srgb, rgba_float_srgb, 0b01010101), _mm_setr_ps(0.3292830288410186767578125f,    0.9195404052734375f,               0.08801330626010894775390625f,    0.0f)),
                       _mm_mul_ps(_mm_shuffle_ps(rgba_float_srgb, rgba_float_srgb, 0b10101010), _mm_setr_ps(0.0433130674064159393310546875f, 0.011362315155565738677978515625f, 0.895595252513885498046875f,      0.0f)))));

        // Convert linear to PQ
        // PQ constants as per Rec. ITU-R BT.2100-3 Table 4
        constexpr float PQ_m1 = 0.1593017578125f;
        constexpr float PQ_m2 = 78.84375f;
        constexpr float PQ_c1 = 0.8359375f;
        constexpr float PQ_c2 = 18.8515625f;
        constexpr float PQ_c3 = 18.6875f;

        auto rgba_float_bt2100_pq = _mm_div_ps(rgba_float_bt2100, _mm_set_ps1(125.0f));
        alignas(16) float temp[4];
        _mm_store_ps(temp, rgba_float_bt2100_pq);
        rgba_float_bt2100_pq = _mm_setr_ps(std::powf(temp[0], PQ_m1), std::powf(temp[1], PQ_m1), std::powf(temp[2], PQ_m1), 0.0f);
        rgba_float_bt2100_pq = _mm_div_ps(_mm_add_ps(_mm_mul_ps(_mm_set_ps1(PQ_c2), rgba_float_bt2100_pq), _mm_set_ps1(PQ_c1)), _mm_add_ps(_mm_mul_ps(_mm_set_ps1(PQ_c3), rgba_float_bt2100_pq), _mm_set_ps1(1.0f)));
        _mm_store_ps(temp, rgba_float_bt2100_pq);
        rgba_float_bt2100_pq = _mm_setr_ps(std::powf(temp[0], PQ_m2), std::powf(temp[1], PQ_m2), std::powf(temp[2], PQ_m2), 0.0f);

        // Convert to integers and pack into 16-bit range
        _mm_storel_epi64(reinterpret_cast<__m128i *>(result), _mm_packus_epi32(_mm_cvtps_epi32(_mm_mul_ps(rgba_float_bt2100_pq, _mm_set_ps1(65536.0f))), _mm_setzero_si128()));

        pixel[0] = result[0];
        pixel[1] = result[1];
        pixel[2] = result[2];
    }
}

/**
 * Post-process HDR pixels based on their format
 * Automatically detects the format and applies necessary conversions
 * 
 * @param pixels Pointer to pixel data
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param format The pixel format to process
 */
inline void post_process_hdr(std::uint8_t *pixels, std::uint32_t width, std::uint32_t height, reshade::api::format format) {
    if (format == reshade::api::format::r16g16b16_float) {
        convert_float16_to_pq_uint16(pixels, width, height);
    }
    // Other formats may be added here in the future (e.g., r16g16b16_unorm, etc.)
}

} // namespace hdr_processing
