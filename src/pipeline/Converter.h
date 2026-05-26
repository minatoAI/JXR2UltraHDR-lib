#pragma once

#include "common/Error.h"
#include "common/ImageBuffer.h"
#include "pipeline/JXRDecoder.h"
#include "pipeline/ColorTransform.h"
#include "pipeline/UltraHDREncoder.h"
#include <string>
#include <atomic>
#include <memory>

// High-level conversion pipeline orchestration
class Converter {
public:
    Converter();
    ~Converter();

    // Convert a single file
    ConverterResult ConvertFile(const std::wstring& inputPath,
                                const std::wstring& outputPath,
                                int sdrQuality = 95,
                                int gainMapQuality = 90);

    // Cancel a running conversion
    void Cancel();

    // Check if cancellation was requested
    bool IsCancelled() const { return cancelled_.load(); }

private:
    std::unique_ptr<JXRDecoder> decoder_;
    std::unique_ptr<ColorTransform> colorTransform_;
    std::unique_ptr<UltraHDREncoder> encoder_;
    std::atomic<bool> cancelled_{false};
};
