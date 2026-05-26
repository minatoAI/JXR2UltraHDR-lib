#include "JXRDecoder.h"
#include "common/Error.h"
#include "common/ImageBuffer.h"

// Windows.h must come BEFORE jxrlib headers.
// Both define GUID — let Windows SDK go first so jxrlib's guiddef.h sees GUID_DEFINED already set.
#include <windows.h>

// jxrlib headers — JXRGlue.h includes <guiddef.h> which is a no-op if GUID already defined
#include <windowsmediaphoto.h>
#include <JXRGlue.h>

#include <string>
#include <vector>
#include <cctype>

// ════════════════════════════════════════════════════════════════
// Internal state
// ════════════════════════════════════════════════════════════════
struct JXRDecoder::Impl {
    // Nothing to persist across calls — resources are created/destroyed per Decode()
};

// ════════════════════════════════════════════════════════════════
JXRDecoder::JXRDecoder() : impl_(std::make_unique<Impl>()) {}
JXRDecoder::~JXRDecoder() = default;

// ── Helper: map jxrlib pixel format GUID to human-readable description ──
static void DescribePixelFormat(const PKPixelFormatGUID& guid, std::wstring& outFormat, std::wstring& outColorSpace, bool& outHDR)
{
    outHDR = false;
    if (guid == GUID_PKPixelFormat64bppRGBAHalf) {
        outFormat = L"RGBA 16-bit Half Float";
        outColorSpace = L"scRGB (HDR)";
        outHDR = true;
    } else if (guid == GUID_PKPixelFormat64bppRGBHalf) {
        outFormat = L"RGB 16-bit Half Float";
        outColorSpace = L"scRGB (HDR)";
        outHDR = true;
    } else if (guid == GUID_PKPixelFormat48bppRGBHalf) {
        outFormat = L"RGB 16-bit Half Float";
        outColorSpace = L"scRGB (HDR)";
        outHDR = true;
    } else if (guid == GUID_PKPixelFormat128bppRGBAFloat) {
        outFormat = L"RGBA 32-bit Float";
        outColorSpace = L"scRGB (HDR)";
        outHDR = true;
    } else if (guid == GUID_PKPixelFormat32bppGrayFloat) {
        outFormat = L"Gray 32-bit Float";
        outColorSpace = L"scRGB (HDR)";
        outHDR = true;
    } else if (guid == GUID_PKPixelFormat16bppGrayHalf) {
        outFormat = L"Gray 16-bit Half Float";
        outColorSpace = L"scRGB (HDR)";
        outHDR = true;
    } else if (guid == GUID_PKPixelFormat32bppRGBE) {
        outFormat = L"RGB 32-bit Shared Exponent";
        outColorSpace = L"HDR";
        outHDR = true;
    } else if (guid == GUID_PKPixelFormat48bppRGBFixedPoint || guid == GUID_PKPixelFormat96bppRGBFixedPoint || guid == GUID_PKPixelFormat96bppRGBFloat) {
        outFormat = L"RGB Fixed-Point";
        outColorSpace = L"scRGB (HDR)";
        outHDR = true;
    } else if (guid == GUID_PKPixelFormat64bppRGBA || guid == GUID_PKPixelFormat64bppPRGBA || guid == GUID_PKPixelFormat64bppRGBAFixedPoint) {
        outFormat = L"RGBA 16-bit";
        outColorSpace = L"sRGB (SDR)";
    } else if (guid == GUID_PKPixelFormat32bppRGBA || guid == GUID_PKPixelFormat32bppBGRA || guid == GUID_PKPixelFormat32bppPBGRA || guid == GUID_PKPixelFormat32bppPRGBA || guid == GUID_PKPixelFormat32bppRGB) {
        outFormat = L"RGBA 8-bit";
        outColorSpace = L"sRGB (SDR)";
    } else if (guid == GUID_PKPixelFormat24bppRGB || guid == GUID_PKPixelFormat24bppBGR) {
        outFormat = L"RGB 8-bit";
        outColorSpace = L"sRGB (SDR)";
    } else if (guid == GUID_PKPixelFormat48bppRGB) {
        outFormat = L"RGB 16-bit";
        outColorSpace = L"sRGB (SDR)";
    } else if (guid == GUID_PKPixelFormat16bppGray || guid == GUID_PKPixelFormat8bppGray) {
        outFormat = L"Gray 8/16-bit";
        outColorSpace = L"sRGB (SDR)";
    } else if (guid == GUID_PKPixelFormatBlackWhite) {
        outFormat = L"Black & White";
        outColorSpace = L"Monochrome";
    } else if (guid == GUID_PKPixelFormat32bppCMYK) {
        outFormat = L"CMYK 32-bit";
        outColorSpace = L"CMYK";
    } else if (guid == GUID_PKPixelFormat16bppRGB555 || guid == GUID_PKPixelFormat16bppRGB565) {
        outFormat = L"RGB 16-bit 555/565";
        outColorSpace = L"sRGB (SDR)";
    } else {
        outFormat = L"Unknown";
        outColorSpace = L"Unknown";
    }
}

// ════════════════════════════════════════════════════════════════
// Lightweight metadata — header decode only, no pixel copy
// ════════════════════════════════════════════════════════════════
ConverterResult JXRDecoder::GetImageInfo(const std::wstring& filePath, ImageInfo& outInfo) {
    HANDLE          hFile  = INVALID_HANDLE_VALUE;
    std::vector<U8> fileBuffer;
    PKFactory*      pkFactory    = nullptr;
    WMPStream*      pStream      = nullptr;
    PKCodecFactory* pCodecFactory = nullptr;
    PKImageDecode*  pDecoder     = nullptr;
    ERR err = WMP_errSuccess;

    // ── 1. Read file into memory (reuses the same approach as Decode) ──
    hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return ConverterResult::Fail(ConverterErrorCode::kFileOpenFailed, L"Failed to open file");

    LARGE_INTEGER liSize{};
    if (!GetFileSizeEx(hFile, &liSize) || liSize.QuadPart > UINT32_MAX || liSize.QuadPart == 0) {
        CloseHandle(hFile);
        return ConverterResult::Fail(ConverterErrorCode::kFileOpenFailed, L"Invalid file size");
    }
    DWORD fileSize = static_cast<DWORD>(liSize.QuadPart);
    fileBuffer.resize(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, fileBuffer.data(), fileSize, &bytesRead, nullptr) || bytesRead != fileSize) {
        CloseHandle(hFile);
        return ConverterResult::Fail(ConverterErrorCode::kFileOpenFailed, L"Failed to read file");
    }
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    // ── 2. PKFactory → memory stream ──
    err = PKCreateFactory(&pkFactory, WMP_SDK_VERSION);
    if (err != WMP_errSuccess)
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed, L"Failed to create PKFactory");
    err = pkFactory->CreateStreamFromMemory(&pStream, fileBuffer.data(), fileBuffer.size());
    pkFactory->Release(&pkFactory);
    pkFactory = nullptr;
    if (err != WMP_errSuccess)
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed, L"Failed to create memory stream");

    // ── 3. Extension → decoder IID ──
    auto dotPos = filePath.find_last_of(L'.');
    if (dotPos == std::wstring::npos || dotPos + 1 >= filePath.size()) {
        pStream->Close(&pStream);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed, L"No file extension");
    }
    std::wstring extW = filePath.substr(dotPos);
    std::string  ext(extW.size(), '\0');
    for (size_t i = 0; i < extW.size(); i++)
        ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(extW[i])));
    const PKIID* pIID = nullptr;
    err = GetImageDecodeIID(ext.c_str(), &pIID);
    if (err != WMP_errSuccess) {
        pStream->Close(&pStream);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed, L"Unsupported format");
    }

    // ── 4. Codec factory + decoder ──
    err = PKCreateCodecFactory(&pCodecFactory, WMP_SDK_VERSION);
    if (err != WMP_errSuccess) {
        pStream->Close(&pStream);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed, L"Failed to create codec factory");
    }
    err = PKCodecFactory_CreateCodec(pIID, reinterpret_cast<void**>(&pDecoder));
    if (err != WMP_errSuccess) {
        pStream->Close(&pStream);
        pCodecFactory->Release(&pCodecFactory);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed, L"Failed to create decoder");
    }

    // ── 5. Initialize decoder with memory stream ──
    err = pDecoder->Initialize(pDecoder, pStream);
    if (err != WMP_errSuccess) {
        pDecoder->Release(&pDecoder);
        pStream->Close(&pStream);
        pCodecFactory->Release(&pCodecFactory);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed, L"Failed to initialize decoder");
    }
    pDecoder->fStreamOwner = TRUE;
    pStream = nullptr;  // decoder handles Close on Release

    // ── 6. Read dimensions ──
    I32 width = 0, height = 0;
    err = pDecoder->GetSize(pDecoder, &width, &height);
    if (err != WMP_errSuccess) {
        pDecoder->Release(&pDecoder);
        pCodecFactory->Release(&pCodecFactory);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed, L"Failed to get image size");
    }

    // ── 7. Read pixel format ──
    PKPixelFormatGUID pfGuid;
    err = pDecoder->GetPixelFormat(pDecoder, &pfGuid);
    if (err != WMP_errSuccess) {
        pDecoder->Release(&pDecoder);
        pCodecFactory->Release(&pCodecFactory);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed, L"Failed to get pixel format");
    }

    outInfo.width = static_cast<int>(width);
    outInfo.height = static_cast<int>(height);
    DescribePixelFormat(pfGuid, outInfo.pixelFormat, outInfo.colorSpace, outInfo.isHDR);

    // ── 8. Release (no pixel decoding was done) ──
    pDecoder->Release(&pDecoder);   // also closes memory stream
    pCodecFactory->Release(&pCodecFactory);

    return ConverterResult::Success();
}

// ════════════════════════════════════════════════════════════════
// Full decode: file → ImageBuffer (RGBA F16)
// ════════════════════════════════════════════════════════════════
ConverterResult JXRDecoder::Decode(const std::wstring& filePath, ImageBuffer& outBuffer) {
    HANDLE          hFile  = INVALID_HANDLE_VALUE;
    std::vector<U8> fileBuffer;
    PKFactory*      pkFactory    = nullptr;
    WMPStream*      pStream      = nullptr;
    PKCodecFactory* pCodecFactory = nullptr;
    PKImageDecode*  pDecoder     = nullptr;
    PKFormatConverter* pConverter = nullptr;
    ERR err = WMP_errSuccess;

    // ── 1. Read file into memory (Unicode-safe: uses CreateFileW) ──
    //     fileBuffer stays alive until the very end of this function,
    //     outliving the memory stream that points into it.
    hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return ConverterResult::Fail(ConverterErrorCode::kFileOpenFailed,
                                     L"Failed to open JXR file");

    LARGE_INTEGER liSize{};
    if (!GetFileSizeEx(hFile, &liSize) || liSize.QuadPart > UINT32_MAX ||
        liSize.QuadPart == 0) {
        CloseHandle(hFile);
        return ConverterResult::Fail(ConverterErrorCode::kFileOpenFailed,
                                     L"Invalid JXR file size");
    }

    DWORD fileSize = static_cast<DWORD>(liSize.QuadPart);
    fileBuffer.resize(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, fileBuffer.data(), fileSize, &bytesRead, nullptr) ||
        bytesRead != fileSize) {
        CloseHandle(hFile);
        return ConverterResult::Fail(ConverterErrorCode::kFileOpenFailed,
                                     L"Failed to read JXR file data");
    }
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    // ── 2. Create PKFactory → memory stream ──
    err = PKCreateFactory(&pkFactory, WMP_SDK_VERSION);
    if (err != WMP_errSuccess)
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed,
                                     L"Failed to create jxrlib PKFactory");

    err = pkFactory->CreateStreamFromMemory(&pStream, fileBuffer.data(),
                                            fileBuffer.size());
    pkFactory->Release(&pkFactory);
    pkFactory = nullptr;
    if (err != WMP_errSuccess)
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed,
                                     L"Failed to create memory stream from buffer");

    // ── 3. Determine image type from file extension ──
    auto dotPos = filePath.find_last_of(L'.');
    if (dotPos == std::wstring::npos || dotPos + 1 >= filePath.size()) {
        pStream->Close(&pStream);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed,
                                     L"File has no extension — cannot determine decoder type");
    }

    // Pass extension WITH the dot, e.g. ".jxr" — GetImageDecodeIID's lookup table expects it
    std::wstring extW = filePath.substr(dotPos);
    std::string  ext(extW.size(), '\0');
    for (size_t i = 0; i < extW.size(); i++)
        ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(extW[i])));

    const PKIID* pIID = nullptr;
    err = GetImageDecodeIID(ext.c_str(), &pIID);
    if (err != WMP_errSuccess) {
        pStream->Close(&pStream);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed,
                                     L"Unsupported image format (extension not recognized)");
    }

    // ── 4. Create codec factory + decoder instance ──
    err = PKCreateCodecFactory(&pCodecFactory, WMP_SDK_VERSION);
    if (err != WMP_errSuccess) {
        pStream->Close(&pStream);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed,
                                     L"Failed to create jxrlib codec factory");
    }

    err = PKCodecFactory_CreateCodec(pIID, reinterpret_cast<void**>(&pDecoder));
    if (err != WMP_errSuccess) {
        pStream->Close(&pStream);
        pCodecFactory->Release(&pCodecFactory);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed,
                                     L"Failed to create decoder instance");
    }

    // ── 5. Initialize decoder with memory stream ──
    err = pDecoder->Initialize(pDecoder, pStream);
    if (err != WMP_errSuccess) {
        pDecoder->Release(&pDecoder);
        pStream->Close(&pStream);
        pCodecFactory->Release(&pCodecFactory);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed,
                                     L"Failed to initialize decoder with memory stream");
    }

    // Transfer stream ownership to decoder (same as CreateDecoderFromFile does)
    pDecoder->fStreamOwner = TRUE;
    pStream = nullptr;  // decoder handles Close on Release

    // ── 6. Get image dimensions ──
    I32 width = 0, height = 0;
    err = pDecoder->GetSize(pDecoder, &width, &height);
    if (err != WMP_errSuccess) {
        pDecoder->Release(&pDecoder);
        pCodecFactory->Release(&pCodecFactory);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed,
                                     L"Failed to get image dimensions");
    }

    // ── 7. Allocate output buffer BEFORE Copy ──
    outBuffer = ImageBuffer(static_cast<int>(width), static_cast<int>(height));

    // ── 8. Create format converter → target RGBA F16 ──
    err = pCodecFactory->CreateFormatConverter(&pConverter);
    if (err != WMP_errSuccess) {
        pDecoder->Release(&pDecoder);
        pCodecFactory->Release(&pCodecFactory);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed,
                                     L"Failed to create format converter");
    }

    err = pConverter->Initialize(pConverter, pDecoder, nullptr,
                                 GUID_PKPixelFormat64bppRGBAHalf);
    if (err == WMP_errUnsupportedFormat) {
        pConverter->Release(&pConverter);
        pDecoder->Release(&pDecoder);
        pCodecFactory->Release(&pCodecFactory);
        return ConverterResult::Fail(ConverterErrorCode::kUnsupportedPixelFormat,
                                     L"JXR pixel format not supported by converter");
    }
    if (err != WMP_errSuccess) {
        pConverter->Release(&pConverter);
        pDecoder->Release(&pDecoder);
        pCodecFactory->Release(&pCodecFactory);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed,
                                     L"Failed to initialize format converter");
    }

    // ── 9. Copy (decode + convert in one call) ──
    PKRect rect = {0, 0, static_cast<I32>(outBuffer.width),
                   static_cast<I32>(outBuffer.height)};
    err = pConverter->Copy(pConverter, &rect,
                           reinterpret_cast<U8*>(outBuffer.data.data()),
                           static_cast<U32>(outBuffer.stride()));
    if (err != WMP_errSuccess) {
        pConverter->Release(&pConverter);
        pDecoder->Release(&pDecoder);
        pCodecFactory->Release(&pCodecFactory);
        return ConverterResult::Fail(ConverterErrorCode::kJXRDecodeFailed,
                                     L"Failed to decode/copy pixels");
    }

    // ── 10. Release (reverse order): converter → decoder (closes stream) → codec factory ──
    pConverter->Release(&pConverter);
    pDecoder->Release(&pDecoder);   // also closes the memory stream (fStreamOwner)
    pCodecFactory->Release(&pCodecFactory);

    // fileBuffer destructor runs here — guaranteed after all jxrlib resources are freed
    return ConverterResult::Success();
}
