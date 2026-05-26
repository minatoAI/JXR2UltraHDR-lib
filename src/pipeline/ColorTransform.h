#pragma once

#include "common/Error.h"
#include "common/ImageBuffer.h"

// Linear gamut conversion: scRGB (sRGB primaries) → BT.2020 primaries
// Output remains linear (no PQ OETF). PQ encoding is handled internally
// by libultrahdr.
class ColorTransform {
public:
    ColorTransform();
    ~ColorTransform();

    ConverterResult Transform(const ImageBuffer& input, ImageBuffer& output);
};
