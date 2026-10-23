cmake_minimum_required(VERSION 3.22)

project(Corda VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(third_party/JUCE)

juce_add_plugin(Corda
    COMPANY_NAME "Corda Audio"
    BUNDLE_ID "audio.corda.grand"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Cord
    PLUGIN_CODE Cora
    FORMATS VST3 Standalone
    PRODUCT_NAME "Corda Grand")

target_sources(Corda PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/PianoEngine.cpp)

target_include_directories(Corda PRIVATE src)

target_compile_definitions(Corda PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Corda PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Offline renderer / test bench. Shares the exact same DSP code as the plugin.
add_executable(corda_render
    tools/render_demo.cpp
    src/dsp/PianoEngine.cpp)
target_include_directories(corda_render PRIVATE src)
target_compile_options(corda_render PRIVATE -O3 -ffast-math)
target_compile_definitions(corda_render PRIVATE JUCE_USE_CURL=0)
# The renderer deliberately uses no framework, only the same DSP translation unit.


# Headless UI snapshot tool (used by the design/packaging workflow).
juce_add_console_app(corda_snapshot PRODUCT_NAME "Corda Snapshot")
target_sources(corda_snapshot PRIVATE
    tools/snapshot.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/PianoEngine.cpp)
target_include_directories(corda_snapshot PRIVATE src)
target_compile_definitions(corda_snapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(corda_snapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags)

# Headless DSP regression test.
add_executable(corda_selftest tools/selftest.cpp src/dsp/PianoEngine.cpp)
target_include_directories(corda_selftest PRIVATE src)
target_compile_options(corda_selftest PRIVATE -O2 -ffast-math)
