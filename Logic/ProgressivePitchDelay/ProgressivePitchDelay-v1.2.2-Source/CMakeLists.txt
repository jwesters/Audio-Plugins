cmake_minimum_required(VERSION 3.22)

project(ProgressivePitchDelay VERSION 1.2.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

if (DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${JUCE_PATH}" JUCE)
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    message(FATAL_ERROR
        "JUCE was not found. Configure with -DJUCE_PATH=/absolute/path/to/JUCE, "
        "or place JUCE in a JUCE folder beside this CMakeLists.txt.")
endif()

juce_add_plugin(ProgressivePitchDelay
    COMPANY_NAME "jwesters"
    COMPANY_COPYRIGHT "Copyright 2026 John Westers"
    BUNDLE_ID "com.jwesters.progressivepitchdelay"
    PLUGIN_MANUFACTURER_CODE Jwst
    PLUGIN_CODE Ppdl
    FORMATS AU VST3
    PRODUCT_NAME "Progressive Pitch Delay"
    DESCRIPTION "Progressive pitch-shifting multi-tap delay with reverse blocks"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    AU_SANDBOX_SAFE TRUE
    VST3_CATEGORIES Fx Delay "Pitch Shift")

target_sources(ProgressivePitchDelay
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/ParameterIDs.h
        Source/Dsp/PhaseVocoderPitchShifter.h
        Source/Dsp/ReverseBlockProcessor.h
        Source/Dsp/SharedDelayBuffer.h)

target_compile_definitions(ProgressivePitchDelay
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ProgressivePitchDelay
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_generate_juce_header(ProgressivePitchDelay)
