#include "UltraHDREncoder.h"
#include "common/Error.h"
#include "common/ImageBuffer.h"

#include <ultrahdr_api.h>

#include <windows.h>
#include <vector>

struct UltraHDREncoder::Impl {
    // No persistent state needed — encoder is created/destroyed per Encode()
};

UltraHDREncoder::UltraHDREncoder() : impl_(std::make_unique<Impl>()) {}
UltraHDREncoder::~UltraHDREncoder() = default;

static ConverterResult WriteFile(const std::wstring& path, const void* data, size_t size) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return ConverterResult::Fail(ConverterErrorCode::kFileWriteFailed,
                                     L"Cannot create output file");
    }
    DWORD written = 0;
    BOOL ok = WriteFile(hFile, data, static_cast<DWORD>(size), &written, nullptr);
    CloseHandle(hFile);
    if (!ok || written != size) {
        return ConverterResult::Fail(ConverterErrorCode::kFileWriteFailed,
                                     L"Failed to write output file");
    }
    return ConverterResult::Success();
}

ConverterResult UltraHDREncoder::Encode(const ImageBuffer& hdrBuffer,
                                        const std::wstring& outputPath,
                                        int sdrQuality,
                                        int gainMapQuality,
                                        int gainMapScaleFactor) {
    if (hdrBuffer.empty() || hdrBuffer.width <= 0 || hdrBuffer.height <= 0) {
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"Input image is empty");
    }

    // ── 1. Describe raw HDR image ──
    uhdr_raw_image_t hdrImg = {};
    hdrImg.fmt = UHDR_IMG_FMT_64bppRGBAHalfFloat;
    hdrImg.cg = UHDR_CG_BT_709;  // scRGB uses sRGB/BT.709 primaries
    hdrImg.ct = UHDR_CT_LINEAR;  // F16 input must use LINEAR (PQ is applied internally)
    hdrImg.range = UHDR_CR_FULL_RANGE;
    hdrImg.w = static_cast<unsigned int>(hdrBuffer.width);
    hdrImg.h = static_cast<unsigned int>(hdrBuffer.height);
    hdrImg.planes[UHDR_PLANE_PACKED] = const_cast<uint16_t*>(hdrBuffer.data.data());
    hdrImg.stride[UHDR_PLANE_PACKED] = static_cast<unsigned int>(hdrBuffer.width);  // in pixels
    hdrImg.stride[UHDR_PLANE_U] = 0;
    hdrImg.stride[UHDR_PLANE_V] = 0;

    // ── 2. Create encoder ──
    uhdr_codec_private_t* enc = uhdr_create_encoder();
    if (!enc) {
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"Failed to create Ultra HDR encoder");
    }

    // ── 3. Set parameters ──
    uhdr_error_info_t err;

    err = uhdr_enc_set_raw_image(enc, &hdrImg, UHDR_HDR_IMG);
    if (err.error_code != UHDR_CODEC_OK) {
        std::string detail(err.has_detail ? err.detail : "");
        std::wstring wdetail(detail.begin(), detail.end());
        uhdr_release_encoder(enc);
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"uhdr_enc_set_raw_image failed: " + wdetail);
    }

    err = uhdr_enc_set_quality(enc, sdrQuality, UHDR_BASE_IMG);
    if (err.error_code != UHDR_CODEC_OK) {
        uhdr_release_encoder(enc);
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"uhdr_enc_set_quality (base) failed");
    }

    err = uhdr_enc_set_quality(enc, gainMapQuality, UHDR_GAIN_MAP_IMG);
    if (err.error_code != UHDR_CODEC_OK) {
        uhdr_release_encoder(enc);
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"uhdr_enc_set_quality (gain map) failed");
    }

    err = uhdr_enc_set_gainmap_scale_factor(enc, gainMapScaleFactor);
    if (err.error_code != UHDR_CODEC_OK) {
        uhdr_release_encoder(enc);
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"uhdr_enc_set_gainmap_scale_factor failed");
    }

    err = uhdr_enc_set_output_format(enc, UHDR_CODEC_JPG);
    if (err.error_code != UHDR_CODEC_OK) {
        uhdr_release_encoder(enc);
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"uhdr_enc_set_output_format failed");
    }

    // Explicitly enable multi-channel gainmap and set content boost for better HDR
    // response. These ensure the gain map metadata signals strong HDR capability.
    err = uhdr_enc_set_using_multi_channel_gainmap(enc, 1);
    if (err.error_code != UHDR_CODEC_OK) {
        uhdr_release_encoder(enc);
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"uhdr_enc_set_using_multi_channel_gainmap failed");
    }

    // Recommend strong content boost to signal meaningful HDR range to decoder
    err = uhdr_enc_set_min_max_content_boost(enc, 1.0f, 25.0f);
    if (err.error_code != UHDR_CODEC_OK) {
        uhdr_release_encoder(enc);
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"uhdr_enc_set_min_max_content_boost failed");
    }

    // Use default gainmap scale factor (1 = full resolution gain map)
    err = uhdr_enc_set_gainmap_scale_factor(enc, 1);
    if (err.error_code != UHDR_CODEC_OK) {
        uhdr_release_encoder(enc);
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"uhdr_enc_set_gainmap_scale_factor failed");
    }

    // Explicitly set target display peak brightness (controls metadata)
    err = uhdr_enc_set_target_display_peak_brightness(enc, 1000.0f);
    if (err.error_code != UHDR_CODEC_OK) {
        uhdr_release_encoder(enc);
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"uhdr_enc_set_target_display_peak_brightness failed");
    }

    // ── 5. Encode ──
    err = uhdr_encode(enc);
    if (err.error_code != UHDR_CODEC_OK) {
        uhdr_release_encoder(enc);
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"uhdr_encode failed");
    }

    // ── 5. Get encoded stream ──
    uhdr_compressed_image_t* outImg = uhdr_get_encoded_stream(enc);
    if (!outImg || !outImg->data || outImg->data_sz == 0) {
        uhdr_release_encoder(enc);
        return ConverterResult::Fail(ConverterErrorCode::kUltraHDREncodingFailed,
                                     L"uhdr_get_encoded_stream returned empty");
    }

    // ── 6. Write to file ──
    ConverterResult writeResult = WriteFile(outputPath, outImg->data, outImg->data_sz);

    // ── 7. Cleanup ──
    uhdr_release_encoder(enc);

    return writeResult;
}
