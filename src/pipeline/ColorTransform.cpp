#include "ColorTransform.h"
#include "common/Error.h"
#include "common/ImageBuffer.h"

#include <cstring>
#include <cstdint>

// ════════════════════════════════════════════════════════════════
// Half-float ↔ float conversion helpers
// ════════════════════════════════════════════════════════════════
static float halfToFloat(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x03FF;

    if (exp == 0) {
        // Denormal or zero → flush to zero (±0.0f)
        uint32_t bits = sign;
        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    } else if (exp == 31) {
        // NaN or Inf — map to float Inf/NaN
        exp = 255;
    } else {
        exp += 127 - 15;  // Bias adjustment: F16 15 → F32 127
    }

    uint32_t bits = sign | (exp << 23) | (mant << 13);
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

static uint16_t floatToHalf(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));

    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exp = static_cast<int32_t>((bits >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = (bits >> 13) & 0x03FF;

    if (exp <= 0) {
        // Flush denormal/subnormal to zero
        return static_cast<uint16_t>(sign);
    } else if (exp >= 31) {
        // Inf or NaN → Sat to F16 inf with max mantissa
        return static_cast<uint16_t>(sign | 0x7C00 | (mant != 0 ? 0x0200 : 0));
    }

    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | mant);
}

// ════════════════════════════════════════════════════════════════
// Colorimetric constants
// ════════════════════════════════════════════════════════════════

// sRGB (BT.709) primaries → BT.2020 primaries (linear-to-linear 3×3 matrix)
// scRGB uses sRGB/BT.709 gamut in linear space.
// Source: ITU-R BT.2407-0
static const float kMatrix709To2020[9] = {
    0.6274039f, 0.3292830f, 0.0433131f,
    0.0690973f, 0.9195404f, 0.0113623f,
    0.0163916f, 0.0880133f, 0.8955951f
};

// ════════════════════════════════════════════════════════════════
ColorTransform::ColorTransform() = default;

ColorTransform::~ColorTransform() = default;

ConverterResult ColorTransform::Transform(const ImageBuffer& input, ImageBuffer& output) {
    if (input.empty() || input.width <= 0 || input.height <= 0) {
        return ConverterResult::Fail(ConverterErrorCode::kColorTransformFailed,
                                     L"Input image is empty");
    }

    // Allocate output buffer (same size, same channel count)
    output = ImageBuffer(input.width, input.height);

    const int numPixels = input.width * input.height;

    for (int i = 0; i < numPixels; ++i) {
        const uint16_t* src = &input.data[i * 4];
        uint16_t* dst = &output.data[i * 4];

        // Step 1: F16 → float for RGB channels
        float r = halfToFloat(src[0]);
        float g = halfToFloat(src[1]);
        float b = halfToFloat(src[2]);

        // Step 2: Linear gamut matrix: scRGB (sRGB primaries) → BT.2020
        float r2 = kMatrix709To2020[0] * r + kMatrix709To2020[1] * g + kMatrix709To2020[2] * b;
        float g2 = kMatrix709To2020[3] * r + kMatrix709To2020[4] * g + kMatrix709To2020[5] * b;
        float b2 = kMatrix709To2020[6] * r + kMatrix709To2020[7] * g + kMatrix709To2020[8] * b;

        // Step 3: Write back as F16, alpha passthrough unchanged
        // Output is linear BT.2100 (BT.2020 primaries). PQ encoding is handled
        // internally by libultrahdr during tone mapping.
        dst[0] = floatToHalf(r2);
        dst[1] = floatToHalf(g2);
        dst[2] = floatToHalf(b2);
        dst[3] = src[3];  // Alpha: bit-copy unchanged
    }

    return ConverterResult::Success();
}
