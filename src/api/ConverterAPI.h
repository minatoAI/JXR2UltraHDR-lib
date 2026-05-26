#pragma once
#ifndef JXR2ULTRADHR_CONVERTER_API_H
#define JXR2ULTRADHR_CONVERTER_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Error codes ──
#define CONVERTER_SUCCESS              0
#define CONVERTER_ERR_FILE_OPEN       -1
#define CONVERTER_ERR_JXR_DECODE      -2
#define CONVERTER_ERR_UNSUPPORTED_FMT -3
#define CONVERTER_ERR_COLOR_TRANSFORM -4
#define CONVERTER_ERR_UHDR_ENCODE     -5
#define CONVERTER_ERR_FILE_WRITE      -6
#define CONVERTER_ERR_OUT_OF_MEMORY   -7
#define CONVERTER_ERR_CANCELLED       -8
#define CONVERTER_ERR_UNKNOWN         -99

// ── Progress callback ──
// Called from worker thread during conversion.
// Return non-zero to cancel the conversion.
typedef int (*Converter_ProgressCallback)(float progress, void* userData);

// ── API functions ──

/// Convert a JXR image file to Ultra HDR JPEG.
///
/// @param inputPath       Input .jxr or .wdp file path (UTF-16 null-terminated)
/// @param outputPath      Output .jpg file path (UTF-16 null-terminated)
/// @param sdrQuality      JPEG SDR quality (1–100; 95 recommended)
/// @param gainMapQuality  Gain-map quality (1–100; 90 recommended)
/// @param callback        Optional progress callback (may be NULL)
/// @param userData        Opaque pointer forwarded to callback
///
/// @return 0 on success, negative error code on failure.
///         Call Converter_GetLastError() for a descriptive message.
__declspec(dllexport) int Converter_ConvertFile(
    const wchar_t* inputPath,
    const wchar_t* outputPath,
    int            sdrQuality,
    int            gainMapQuality,
    Converter_ProgressCallback callback,
    void* userData
);

/// Cancel a running conversion (thread-safe).
__declspec(dllexport) void Converter_Cancel(void);

/// Check if cancellation was requested (thread-safe).
/// Returns non-zero if cancelled.
__declspec(dllexport) int Converter_IsCancelled(void);

/// Return the last error message for the calling thread.
/// The returned string is thread-local; do not free it.
/// Returns an empty string (L"") on success.
__declspec(dllexport) const wchar_t* Converter_GetLastError(void);

/// Image metadata result (returned by Converter_GetImageInfo).
typedef struct ConverterImageInfo {
    int            width;
    int            height;
    int            isHDR;           // 0 or 1
    const wchar_t* pixelFormat;     // human-readable description
    const wchar_t* colorSpace;      // human-readable description
} ConverterImageInfo;

/// Read image metadata (dimensions, pixel format) without full decode.
/// File header is parsed — no pixel copying, fast.
/// @return 0 on success, negative error code on failure.
__declspec(dllexport) int Converter_GetImageInfo(
    const wchar_t*   filePath,
    ConverterImageInfo* info
);

#ifdef __cplusplus
}
#endif

#endif // JXR2ULTRADHR_CONVERTER_API_H
