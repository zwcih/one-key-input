find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

set(ONEKEY_CORE_TEST_SOURCES
    util/StringsTest.cpp
    util/WinHelpersTest.cpp
    config/ConfigTest.cpp
    session/EventBusTest.cpp
    session/HotkeyBehaviorMachineTest.cpp
    asr/AsrFactoryTest.cpp
    asr/SherpaAsrEngineTest.cpp
    polish/PolisherFactoryTest.cpp
    polish/TranslationPromptTest.cpp
    inject/InjectorStrategyTest.cpp
    hotkey/HotkeyManagerTest.cpp
)

add_executable(onekey-core-tests ${ONEKEY_CORE_TEST_SOURCES})

target_link_libraries(onekey-core-tests PRIVATE
    onekey-core-obj
    GTest::gtest
    GTest::gtest_main
)

# Console subsystem for the test runner — onekey-core-obj inherits headers
# / link libs from the OBJECT lib; we still want a console for gtest output.
set_target_properties(onekey-core-tests PROPERTIES WIN32_EXECUTABLE OFF)
if(MSVC)
    target_link_options(onekey-core-tests PRIVATE "/SUBSYSTEM:CONSOLE")
endif()

# Speech SDK runtime DLLs need to sit next to the test exe so any test
# whose code path eagerly resolves SDK symbols can load.
foreach(_dll
    Microsoft.CognitiveServices.Speech.core.dll
    Microsoft.CognitiveServices.Speech.extension.audio.sys.dll
    Microsoft.CognitiveServices.Speech.extension.codec.dll
    Microsoft.CognitiveServices.Speech.extension.kws.dll
    Microsoft.CognitiveServices.Speech.extension.kws.ort.dll
    Microsoft.CognitiveServices.Speech.extension.lu.dll)
    add_custom_command(TARGET onekey-core-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OK_SPEECH_SDK_DLL_DIR}/${_dll}"
            "$<TARGET_FILE_DIR:onekey-core-tests>/${_dll}"
        VERBATIM
    )
endforeach()

# Same for sherpa-onnx + onnxruntime — SherpaAsrEngine constructor doesn't
# touch the SDK (it only validates config paths) but the import library is
# still in the link, so the loader will resolve sherpa-onnx-c-api.dll at exe
# startup.
foreach(_dll ${SHERPA_ONNX_RUNTIME_DLLS})
    add_custom_command(TARGET onekey-core-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_dll}"
            "$<TARGET_FILE_DIR:onekey-core-tests>/"
        VERBATIM
    )
endforeach()

gtest_discover_tests(onekey-core-tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
