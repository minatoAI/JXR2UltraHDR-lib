#pragma once

#include <string>

enum class ConverterErrorCode : int {
    kSuccess = 0,
    kFileOpenFailed,
    kJXRDecodeFailed,
    kUnsupportedPixelFormat,
    kColorTransformFailed,
    kUltraHDREncodingFailed,
    kFileWriteFailed,
    kOutOfMemory,
    kCancelled,
};

struct ConverterResult {
    ConverterErrorCode errorCode = ConverterErrorCode::kSuccess;
    std::wstring message;

    bool ok() const { return errorCode == ConverterErrorCode::kSuccess; }

    static ConverterResult Success() {
        return {ConverterErrorCode::kSuccess, L""};
    }

    static ConverterResult Fail(ConverterErrorCode code, const std::wstring& msg) {
        return {code, msg};
    }
};
