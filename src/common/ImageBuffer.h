#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// RGBA F16 pixel buffer (half-float)
struct ImageBuffer {
    int width = 0;
    int height = 0;
    int channels = 4;  // RGBA
    std::vector<uint16_t> data;  // F16 per channel, stride = width * 8

    ImageBuffer() = default;

    ImageBuffer(int w, int h) : width(w), height(h) {
        data.resize(static_cast<size_t>(w) * h * 4);
    }

    bool empty() const { return data.empty(); }

    size_t stride() const { return static_cast<size_t>(width) * 8; }  // 8 bytes per pixel (4ch * 16bit)

    uint16_t* pixel(int x, int y) {
        return &data[static_cast<size_t>(y) * width * 4 + static_cast<size_t>(x) * 4];
    }

    const uint16_t* pixel(int x, int y) const {
        return &data[static_cast<size_t>(y) * width * 4 + static_cast<size_t>(x) * 4];
    }
};
