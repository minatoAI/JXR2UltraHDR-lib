#include "Converter.h"

Converter::Converter()
    : decoder_(std::make_unique<JXRDecoder>())
    , colorTransform_(std::make_unique<ColorTransform>())
    , encoder_(std::make_unique<UltraHDREncoder>())
{}

Converter::~Converter() = default;

void Converter::Cancel() {
    cancelled_.store(true);
}

ConverterResult Converter::ConvertFile(const std::wstring& inputPath,
                                       const std::wstring& outputPath,
                                       int sdrQuality,
                                       int gainMapQuality) {
    if (cancelled_.load()) {
        return ConverterResult::Fail(ConverterErrorCode::kCancelled, L"Conversion cancelled");
    }

    // Step 1: Decode JXR → scRGB RGBA F16
    ImageBuffer decoded;
    auto result = decoder_->Decode(inputPath, decoded);
    if (!result.ok()) return result;

    if (cancelled_.load()) {
        return ConverterResult::Fail(ConverterErrorCode::kCancelled, L"Conversion cancelled");
    }

    // Step 2: Encode to Ultra HDR .jpg
    // Note: color transform (scRGB→BT.2020) is currently skipped.
    // Pixel data is passed as-is (scRGB linear F16) with color gamut=BT.709
    // (same primitives as scRGB/sRGB). libultrahdr handles internal tone mapping.
    result = encoder_->Encode(decoded, outputPath, sdrQuality, gainMapQuality);
    if (!result.ok()) return result;

    // Step 3 (future): Color transform stage will be re-enabled
    // once the gamut mapping pipeline is debugged.
    // ColorTransform works on ImageBuffer: scRGB linear → BT.2020 linear

    return ConverterResult::Success();
}
