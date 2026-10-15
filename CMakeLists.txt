cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel) unless the caller overrides it.
if(APPLE)
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

project(EVOX VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(EVOX_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU/Standalone)" ON)
option(EVOX_BUILD_TESTS  "Build unit tests and the offline analysis harness" ON)

# ---------------------------------------------------------------------------
# DSP core: pure C++20, no JUCE. Shared by the plugin, tests and offline tools,
# and by any future AAX wrapper.
# ---------------------------------------------------------------------------
add_library(evox_dsp STATIC
    source/dsp/EvoxEngine.cpp
    source/dsp/Fft.cpp
    source/dsp/PitchAnalyzer.cpp
    source/dsp/PitchCorrector.cpp
    source/dsp/PitchMarker.cpp
    source/dsp/PsolaShifter.cpp
    source/dsp/YinPitchDetector.cpp)
target_include_directories(evox_dsp PUBLIC source)
target_compile_features(evox_dsp PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(evox_dsp PRIVATE /W4 /fp:fast)
else()
    target_compile_options(evox_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(EVOX_BUILD_PLUGIN)
    include(cmake/FetchJUCE.cmake)

    set(EVOX_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND EVOX_FORMATS AU)
    endif()
    # AAX: add "AAX" here and call juce_set_aax_sdk_path() once you have the SDK.

    juce_add_plugin(EVOX
        COMPANY_NAME "Lotus Sounds"
        COMPANY_WEBSITE "https://lotussounds.example"
        BUNDLE_ID "com.lotussounds.evox"
        PLUGIN_MANUFACTURER_CODE Ltsd
        PLUGIN_CODE Evox
        PRODUCT_NAME "EVOX"
        FORMATS ${EVOX_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Pitch Shift
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(EVOX PRIVATE
        source/plugin/Parameters.cpp
        source/plugin/PluginProcessor.cpp
        source/plugin/PluginEditor.cpp)

    target_include_directories(EVOX PRIVATE source source/plugin)

    target_compile_definitions(EVOX PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(EVOX
        PRIVATE
            evox_dsp
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Tests + offline tools (no JUCE dependency)
# ---------------------------------------------------------------------------
if(EVOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(tools)
endif()
