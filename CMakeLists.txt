cmake_minimum_required(VERSION 3.20)
project(JXR2UltraHDR_lib VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_FOR_WINUI "Build with /MD CRT for WinUI compatibility" ON)

if(MSVC)
    if(BUILD_FOR_WINUI)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# ════════════════════════════════════════════════════════════════
# jxrlib 静态库
# ════════════════════════════════════════════════════════════════
set(JXR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/jxrlib")

add_library(jxrlib STATIC
    # JXRGlueLib
    "${JXR_ROOT}/jxrgluelib/JXRGlue.c"
    "${JXR_ROOT}/jxrgluelib/JXRGluePFC.c"
    "${JXR_ROOT}/jxrgluelib/JXRGlueJxr.c"
    "${JXR_ROOT}/jxrgluelib/JXRMeta.c"
    # DecodeLib
    "${JXR_ROOT}/image/decode/decode.c"
    "${JXR_ROOT}/image/decode/postprocess.c"
    "${JXR_ROOT}/image/decode/segdec.c"
    "${JXR_ROOT}/image/decode/strdec.c"
    "${JXR_ROOT}/image/decode/strdec_x86.c"
    "${JXR_ROOT}/image/decode/strInvTransform.c"
    "${JXR_ROOT}/image/decode/strPredQuantDec.c"
    "${JXR_ROOT}/image/decode/JXRTranscode.c"
    # EncodeLib
    "${JXR_ROOT}/image/encode/encode.c"
    "${JXR_ROOT}/image/encode/segenc.c"
    "${JXR_ROOT}/image/encode/strFwdTransform.c"
    "${JXR_ROOT}/image/encode/strPredQuantEnc.c"
    "${JXR_ROOT}/image/encode/strenc.c"
    "${JXR_ROOT}/image/encode/strenc_x86.c"
    # CommonLib
    "${JXR_ROOT}/image/sys/adapthuff.c"
    "${JXR_ROOT}/image/sys/image.c"
    "${JXR_ROOT}/image/sys/perfTimerANSI.c"
    "${JXR_ROOT}/image/sys/strcodec.c"
    "${JXR_ROOT}/image/sys/strPredQuant.c"
    "${JXR_ROOT}/image/sys/strTransform.c"
)

target_compile_definitions(jxrlib PRIVATE INITGUID)

target_include_directories(jxrlib PUBLIC
    "${JXR_ROOT}"
    "${JXR_ROOT}/jxrgluelib"
    "${JXR_ROOT}/common/include"
    "${JXR_ROOT}/image/sys"
)

if(MSVC)
    target_compile_options(jxrlib PRIVATE /wd4828)
endif()

# ════════════════════════════════════════════════════════════════
# libultrahdr 静态库
# ════════════════════════════════════════════════════════════════
set(UHDR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/libultrahdr")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(UHDR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(UHDR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UHDR_BUILD_DEPS ON CACHE BOOL "" FORCE)
set(UHDR_WRITE_XMP ON CACHE BOOL "" FORCE)

# Apply color-gamut patch before building libultrahdr
set(UHDR_PATCH_FLAG "${UHDR_ROOT}/.patched")
if(NOT EXISTS "${UHDR_PATCH_FLAG}")
    message(STATUS "Applying fix_color_gamut_srgb.patch to libultrahdr ...")
    execute_process(
        COMMAND git apply "${CMAKE_CURRENT_SOURCE_DIR}/patches/fix_color_gamut_srgb.patch"
        WORKING_DIRECTORY "${UHDR_ROOT}"
        RESULT_VARIABLE _patch_result
        ERROR_VARIABLE _patch_error
        OUTPUT_QUIET
    )
    if(_patch_result EQUAL 0)
        file(WRITE "${UHDR_PATCH_FLAG}" "patched")
        message(STATUS "Patch applied successfully")
    else()
        message(WARNING "Patch failed to apply (may already be applied): ${_patch_error}")
    endif()
endif()

add_subdirectory("${UHDR_ROOT}" uhdr_build)

# Fix CRT: libultrahdr forces /MT when BUILD_SHARED_LIBS=OFF on MSVC (line 211).
# When building for WinUI (/MD), override back to /MD for all targets.
if(MSVC AND BUILD_FOR_WINUI)
    # Restore global CRT setting so FetchContent-built deps (turbojpeg) also use /MD
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    # Override per-target
    foreach(tgt uhdr-static core image_io)
        if(TARGET ${tgt})
            set_target_properties(${tgt} PROPERTIES
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
            )
        endif()
    endforeach()
endif()

# ════════════════════════════════════════════════════════════════
# JXR2UltraHDR_core — 核心转换库（静态库）
# ════════════════════════════════════════════════════════════════
add_library(JXR2UltraHDR_core STATIC
    src/api/ConverterAPI.cpp
    src/pipeline/Converter.cpp
    src/pipeline/JXRDecoder.cpp
    src/pipeline/ColorTransform.cpp
    src/pipeline/UltraHDREncoder.cpp
)

if(MSVC)
    target_compile_options(JXR2UltraHDR_core PRIVATE /utf-8)
endif()

target_link_libraries(JXR2UltraHDR_core PUBLIC
    jxrlib
    uhdr-static
)

target_include_directories(JXR2UltraHDR_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/common>
)

set_property(TARGET JXR2UltraHDR_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
