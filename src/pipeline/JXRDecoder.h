#pragma once

#include "common/Error.h"
#include "common/ImageBuffer.h"
#include <string>
#include <memory>

// Wraps jxrlib decoding: .jxr / .wdp → RGBA F16 ImageBuffer (scRGB linear)
// Uses PKFormatConverter with target GUID_PKPixelFormat64bppRGBAHalf
// to handle all JXR pixel formats uniformly.
// Holds lightweight image metadata (no pixel data)
struct ImageInfo {
    int width = 0;
    int height = 0;
    std::wstring pixelFormat;   // human-readable description
    std::wstring colorSpace;    // human-readable description
    bool isHDR = false;
};

class JXRDecoder {
public:
    JXRDecoder();
    ~JXRDecoder();

    ConverterResult Decode(const std::wstring& filePath, ImageBuffer& outBuffer);

    // Lightweight metadata — decodes only the file header, no pixel copy
    ConverterResult GetImageInfo(const std::wstring& filePath, ImageInfo& outInfo);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
