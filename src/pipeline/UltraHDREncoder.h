#pragma once

#include "common/Error.h"
#include "common/ImageBuffer.h"
#include <memory>
#include <vector>

// Wraps libultrahdr API-0 encoding: RGBA F16 (BT.2100 PQ) → .jpg with gain map
class UltraHDREncoder {
public:
    UltraHDREncoder();
    ~UltraHDREncoder();

    ConverterResult Encode(const ImageBuffer& hdrBuffer,
                           const std::wstring& outputPath,
                           int sdrQuality = 95,
                           int gainMapQuality = 90,
                           int gainMapScaleFactor = 4);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
