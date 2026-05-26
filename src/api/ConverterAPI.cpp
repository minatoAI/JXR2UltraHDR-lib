#include "ConverterAPI.h"
#include "pipeline/Converter.h"
#include "pipeline/JXRDecoder.h"

#include <objbase.h>
#include <string>
#include <mutex>
#include <atomic>

// ── Thread-local last error ──
static thread_local std::wstring tls_lastError;

// ── Global cancellation flag ──
static std::atomic<bool> g_cancelled{false};

// ── Helper: waitable wrapper for progress ──
struct ProgressContext {
    Converter_ProgressCallback callback;
    void* userData;
};

static void ProgressThunk(float pct, void* ctx) {
    auto* pc = static_cast<ProgressContext*>(ctx);
    if (pc && pc->callback) {
        if (pc->callback(pct, pc->userData) != 0) {
            g_cancelled.store(true);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
int Converter_ConvertFile(
    const wchar_t* inputPath,
    const wchar_t* outputPath,
    int sdrQuality,
    int gainMapQuality,
    Converter_ProgressCallback callback,
    void* userData)
{
    tls_lastError.clear();
    g_cancelled.store(false);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comInitialized = SUCCEEDED(hr);

    // Validate input
    if (!inputPath || !outputPath) {
        tls_lastError = L"Null path argument";
        if (comInitialized) CoUninitialize();
        return CONVERTER_ERR_FILE_OPEN;
    }

    // Build converter pipeline
    Converter converter;

    // Set up progress (if caller provided a callback)
    ProgressContext pc{callback, userData};

    // Run conversion
    // NOTE: Current Converter::ConvertFile does not support per-frame progress.
    // The callback is here for future CLI-progress integration.
    // If the caller cancels before/during conversion, the cancellation flag
    // is checked at each pipeline stage inside ConvertFile.
    ConverterResult result = converter.ConvertFile(
        inputPath, outputPath, sdrQuality, gainMapQuality);

    if (comInitialized) {
        CoUninitialize();
    }

    if (result.ok()) {
        return CONVERTER_SUCCESS;
    }

    // Map error
    tls_lastError = result.message;
    switch (result.errorCode) {
        case ConverterErrorCode::kFileOpenFailed:        return CONVERTER_ERR_FILE_OPEN;
        case ConverterErrorCode::kJXRDecodeFailed:       return CONVERTER_ERR_JXR_DECODE;
        case ConverterErrorCode::kUnsupportedPixelFormat: return CONVERTER_ERR_UNSUPPORTED_FMT;
        case ConverterErrorCode::kColorTransformFailed:   return CONVERTER_ERR_COLOR_TRANSFORM;
        case ConverterErrorCode::kUltraHDREncodingFailed: return CONVERTER_ERR_UHDR_ENCODE;
        case ConverterErrorCode::kFileWriteFailed:        return CONVERTER_ERR_FILE_WRITE;
        case ConverterErrorCode::kOutOfMemory:            return CONVERTER_ERR_OUT_OF_MEMORY;
        case ConverterErrorCode::kCancelled:              return CONVERTER_ERR_CANCELLED;
        default:                                          return CONVERTER_ERR_UNKNOWN;
    }
}

// ═══════════════════════════════════════════════════════════════
void Converter_Cancel(void) {
    g_cancelled.store(true);
}

// ═══════════════════════════════════════════════════════════════
int Converter_IsCancelled(void) {
    return g_cancelled.load() ? 1 : 0;
}

// ═══════════════════════════════════════════════════════════════
const wchar_t* Converter_GetLastError(void) {
    return tls_lastError.c_str();
}

// ── Thread-local storage for image info strings ──
static thread_local std::wstring tls_infoPixelFormat;
static thread_local std::wstring tls_infoColorSpace;

// ═══════════════════════════════════════════════════════════════
int Converter_GetImageInfo(const wchar_t* filePath, ConverterImageInfo* info)
{
    tls_lastError.clear();
    if (!filePath || !info) {
        tls_lastError = L"Null argument";
        return CONVERTER_ERR_FILE_OPEN;
    }

    JXRDecoder decoder;
    ImageInfo imgInfo;
    auto result = decoder.GetImageInfo(filePath, imgInfo);

    if (!result.ok()) {
        tls_lastError = result.message;
        return CONVERTER_ERR_JXR_DECODE;
    }

    info->width = imgInfo.width;
    info->height = imgInfo.height;
    info->isHDR = imgInfo.isHDR ? 1 : 0;
    tls_infoPixelFormat = std::move(imgInfo.pixelFormat);
    tls_infoColorSpace = std::move(imgInfo.colorSpace);
    info->pixelFormat = tls_infoPixelFormat.c_str();
    info->colorSpace = tls_infoColorSpace.c_str();
    return CONVERTER_SUCCESS;
}
