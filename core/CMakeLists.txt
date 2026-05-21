find_package(cpr CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(wil CONFIG REQUIRED)

# Azure Speech SDK — pulled in by `nuget restore` (see scripts/build.bat).
# We pin the version so the path stays stable; keep this in sync with
# nuget/packages.config.
set(OK_SPEECH_SDK_VERSION "1.43.0")
set(OK_SPEECH_SDK_ROOT
    "${CMAKE_SOURCE_DIR}/nuget/packages/Microsoft.CognitiveServices.Speech.${OK_SPEECH_SDK_VERSION}")
if(NOT EXISTS "${OK_SPEECH_SDK_ROOT}")
    message(FATAL_ERROR
        "Azure Speech SDK not found at ${OK_SPEECH_SDK_ROOT}. "
        "Run scripts/build.bat (it invokes nuget restore) or restore manually.")
endif()
set(OK_SPEECH_SDK_INCLUDE
    "${OK_SPEECH_SDK_ROOT}/build/native/include/cxx_api"
    "${OK_SPEECH_SDK_ROOT}/build/native/include/c_api")
set(OK_SPEECH_SDK_LIB
    "${OK_SPEECH_SDK_ROOT}/build/native/x64/Release/Microsoft.CognitiveServices.Speech.core.lib")
set(OK_SPEECH_SDK_DLL_DIR "${OK_SPEECH_SDK_ROOT}/runtimes/win-x64/native")

# Production sources live in an OBJECT library so the executable AND the
# test binary can both link them without compiling twice.
# main.cpp (the wmain entry point) is kept *out* of the object library so
# tests can supply their own gtest_main without symbol conflicts.
set(ONEKEY_CORE_OBJ_SOURCES
    src/app/Application.cpp
    src/app/Autostart.cpp
    src/app/ConfigWatcher.cpp
    src/app/SelfRestart.cpp
    src/config/Config.cpp
    src/log/Logger.cpp
    src/util/Strings.cpp
    src/util/WinHelpers.cpp
    src/net/HttpClient.cpp
    src/hotkey/HotkeyManager.cpp
    src/audio/WasapiCapture.cpp
    src/audio/SoundCues.cpp
    src/asr/AsrFactory.cpp
    src/asr/IAsrEngine.cpp
    src/asr/AzureRestAsrEngine.cpp
    src/asr/AzureStreamAsrEngine.cpp
    src/polish/PolisherFactory.cpp
    src/polish/OpenAIPolisher.cpp
    src/polish/TranslationPrompt.cpp
    src/inject/InjectorStrategy.cpp
    src/inject/ClipboardInjector.cpp
    src/inject/SendInputUnicodeInjector.cpp
    src/session/DictationSession.cpp
    src/session/EventBus.cpp
    src/focus/FocusContext.cpp
    src/focus/ContextExtractor.cpp
    src/ui/TrayIcon.cpp
    src/ui/IconFactory.cpp
    src/ui/OverlayWindow.cpp
)

add_library(onekey-core-obj OBJECT ${ONEKEY_CORE_OBJ_SOURCES})

target_include_directories(onekey-core-obj PUBLIC src ${OK_SPEECH_SDK_INCLUDE})

target_link_libraries(onekey-core-obj PUBLIC
    cpr::cpr
    spdlog::spdlog
    fmt::fmt
    nlohmann_json::nlohmann_json
    WIL::WIL
    ${OK_SPEECH_SDK_LIB}
    # Win32 SDK
    Avrt
    Ole32
    OleAut32
    Mmdevapi
    Winmm
    User32
    Imm32
    Shell32
    Gdi32
)

add_executable(onekey-core
    src/main.cpp
    resources/onekey.rc
)

target_link_libraries(onekey-core PRIVATE onekey-core-obj)

set_target_properties(onekey-core PROPERTIES
    OUTPUT_NAME "onekey-core"
    WIN32_EXECUTABLE ON
)

# WIN32_EXECUTABLE switches us to the WINDOWS subsystem (no console). We
# still want to keep wmain as the entry point (rather than rewriting as
# wWinMain), so tell the linker explicitly.
if(MSVC)
    target_link_options(onekey-core PRIVATE "/ENTRY:wmainCRTStartup")
endif()

# Copy config.json from the repo root next to the exe at build time, so the
# binary in build/default/bin/ runs out of the box. Config is gitignored —
# users start from config.example.json. Falls back to the example if missing.
set(_VI_CFG "${CMAKE_SOURCE_DIR}/config.json")
if(NOT EXISTS "${_VI_CFG}")
    set(_VI_CFG "${CMAKE_SOURCE_DIR}/config.example.json")
endif()
add_custom_command(TARGET onekey-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_VI_CFG}"
        "$<TARGET_FILE_DIR:onekey-core>/config.json"
    VERBATIM
)

# Copy Speech SDK runtime DLLs next to the exe. Listed explicitly (vs. glob)
# so a stray DLL doesn't sneak into bin/ unnoticed.
foreach(_dll
    Microsoft.CognitiveServices.Speech.core.dll
    Microsoft.CognitiveServices.Speech.extension.audio.sys.dll
    Microsoft.CognitiveServices.Speech.extension.codec.dll
    Microsoft.CognitiveServices.Speech.extension.kws.dll
    Microsoft.CognitiveServices.Speech.extension.kws.ort.dll
    Microsoft.CognitiveServices.Speech.extension.lu.dll)
    add_custom_command(TARGET onekey-core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OK_SPEECH_SDK_DLL_DIR}/${_dll}"
            "$<TARGET_FILE_DIR:onekey-core>/${_dll}"
        VERBATIM
    )
endforeach()

# Unit tests — enabled by default; toggle off with -DONEKEY_BUILD_TESTS=OFF.
option(ONEKEY_BUILD_TESTS "Build onekey-core unit tests" ON)
if(ONEKEY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
