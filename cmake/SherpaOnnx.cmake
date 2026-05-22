## Fetches the official sherpa-onnx prebuilt Windows x64 shared-MD release
## (no-TTS variant: drops TTS models to halve the bundle size). Exposes the
## INTERFACE target `sherpa::sherpa` carrying the C API headers + import lib,
## and the variable `SHERPA_ONNX_RUNTIME_DLLS` listing the DLLs to ship next
## to onekey-core.exe.
##
## Version + hash are pinned so CI is reproducible. To bump:
##   1. Update OK_SHERPA_VERSION below.
##   2. Download the matching tarball and recompute SHA256.
##   3. Skim the upstream c-api.h for breaking renames before merging.

set(OK_SHERPA_VERSION "1.13.2")
set(OK_SHERPA_URL
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/v${OK_SHERPA_VERSION}/sherpa-onnx-v${OK_SHERPA_VERSION}-win-x64-shared-MD-Release-no-tts.tar.bz2")
set(OK_SHERPA_SHA256
    "d74ad2c3e2f943e51ed8b15d409281dea378fcb21f7bb83e8b070be03f2f6715")

include(FetchContent)
FetchContent_Declare(sherpa_onnx_prebuilt
    URL      "${OK_SHERPA_URL}"
    URL_HASH "SHA256=${OK_SHERPA_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sherpa_onnx_prebuilt)

set(OK_SHERPA_ROOT "${sherpa_onnx_prebuilt_SOURCE_DIR}")
if(NOT EXISTS "${OK_SHERPA_ROOT}/include/sherpa-onnx/c-api/c-api.h")
    message(FATAL_ERROR
        "sherpa-onnx prebuilt layout unexpected at ${OK_SHERPA_ROOT}; "
        "did the upstream packaging change?")
endif()

add_library(sherpa_sherpa INTERFACE)
target_include_directories(sherpa_sherpa INTERFACE "${OK_SHERPA_ROOT}/include")
target_link_libraries(sherpa_sherpa INTERFACE
    "${OK_SHERPA_ROOT}/lib/sherpa-onnx-c-api.lib")
add_library(sherpa::sherpa ALIAS sherpa_sherpa)

# DLLs that must sit next to any exe that links sherpa::sherpa. The example
# binaries (sherpa-onnx*.exe) in bin/ are intentionally NOT shipped.
set(SHERPA_ONNX_RUNTIME_DLLS
    "${OK_SHERPA_ROOT}/lib/sherpa-onnx-c-api.dll"
    "${OK_SHERPA_ROOT}/lib/sherpa-onnx-cxx-api.dll"
    "${OK_SHERPA_ROOT}/lib/onnxruntime.dll"
    "${OK_SHERPA_ROOT}/lib/onnxruntime_providers_shared.dll"
    CACHE INTERNAL "Runtime DLLs that must accompany sherpa-onnx-c-api")
