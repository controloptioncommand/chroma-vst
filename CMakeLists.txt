cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures")

project(chroma VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: локально можно передать -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(CHROMA_FORMATS VST3 AU Standalone)
else()
    set(CHROMA_FORMATS VST3 Standalone)
endif()

juce_add_plugin(chroma
    COMPANY_NAME "Personal"
    BUNDLE_ID "com.personal.chroma"
    PLUGIN_MANUFACTURER_CODE Prsn
    PLUGIN_CODE Chrm
    FORMATS ${CHROMA_FORMATS}
    PRODUCT_NAME "chroma"
    PLUGIN_NAME "chroma"
    DESCRIPTION "Rack of experimental effects"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx EQ Modulation
    AU_MAIN_TYPE kAudioUnitType_Effect)

set(CHROMA_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp
    Source/Presets.cpp
    Source/Randomizer.cpp
    Source/EffectPresets.cpp
    Source/DSP/Ophan.cpp
    Source/DSP/OttStack.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/Widgets.cpp
    Source/UI/Views.cpp
    Source/UI/Rack.cpp
    Source/UI/OphanPanel.cpp
    Source/UI/About.cpp
    Source/UI/Credits.cpp)

target_sources(chroma PRIVATE ${CHROMA_SOURCES})
target_include_directories(chroma PRIVATE Source)

set(CHROMA_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(chroma PUBLIC ${CHROMA_DEFS})

# Шрифты Medeo (SIL OFL): Sans — интерфейс, Mono — числа, Pixel — логотип
juce_add_binary_data(ChromaFonts
    NAMESPACE ChromaFonts
    HEADER_NAME BinaryData.h
    SOURCES
        Resources/Fonts/MedeoSans-Regular.ttf
        Resources/Fonts/MedeoSans-SemiBold.ttf
        Resources/Fonts/MedeoMono-Regular.ttf
        Resources/Fonts/MedeoMono-SemiBold.ttf
        Resources/Fonts/MedeoPixel-Regular.ttf)
set_target_properties(ChromaFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(CHROMA_LIBS
    ChromaFonts
    juce::juce_audio_utils
    juce::juce_dsp)

target_link_libraries(chroma
    PRIVATE ${CHROMA_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Офлайн-тест: обработка в WAV, проверки звука, состояния и скриншоты интерфейса
option(CHROMA_BUILD_TESTS "Build offline test" OFF)
if(CHROMA_BUILD_TESTS)
    juce_add_console_app(ChromaTest PRODUCT_NAME "ChromaTest")
    target_sources(ChromaTest PRIVATE Tests/ChromaTest.cpp ${CHROMA_SOURCES})
    target_include_directories(ChromaTest PRIVATE Source)
    target_compile_definitions(ChromaTest PRIVATE ${CHROMA_DEFS} CHROMA_TEST_BUILD=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChromaTest PRIVATE ${CHROMA_LIBS} juce::juce_recommended_config_flags)
endif()
