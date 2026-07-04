cmake_minimum_required(VERSION 3.22)
project(Custos VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(CUSTOS_HARDCODED_SYNTH_PATH "" CACHE STRING
    "Absolute path to a VST3 synth Custos loads at boot (M1). Empty = silent passthrough.")

# ---- Core library shared by plugin + tests --------------------------------
add_library(custos_core STATIC
    src/FacadeParameter.cpp
    src/InnerBinding.cpp
    src/SynthLoader.cpp
    src/StateCodec.cpp
    src/HostTrace.cpp
    src/SynthWindow.cpp
    src/CustosEditor.cpp
    src/CustosProcessor.cpp
    src/CustosOscServer.cpp)
target_include_directories(custos_core PUBLIC src)
target_compile_features(custos_core PUBLIC cxx_std_17)
target_compile_definitions(custos_core PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)
target_compile_definitions(custos_core PUBLIC
    CUSTOS_OSC_PORT=9100
    CUSTOS_ACK_HOST="127.0.0.1"
    CUSTOS_ACK_PORT=8000)
target_link_libraries(custos_core
    PUBLIC
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_osc
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
if(CUSTOS_HARDCODED_SYNTH_PATH)
    target_compile_definitions(custos_core PUBLIC
        CUSTOS_HARDCODED_SYNTH_PATH="${CUSTOS_HARDCODED_SYNTH_PATH}")
endif()

option(CUSTOS_HOST_TRACE "Write a timestamped host-call trace log (GP count-timing experiment)" ON)
if(CUSTOS_HOST_TRACE)
    target_compile_definitions(custos_core PUBLIC CUSTOS_HOST_TRACE=1)
endif()

# ---- The plugin -----------------------------------------------------------
juce_add_plugin(Custos
    PRODUCT_NAME "Custos"
    COMPANY_NAME "Kapellmeister"
    PLUGIN_MANUFACTURER_CODE Kplm
    PLUGIN_CODE Cst1
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)
target_sources(Custos PRIVATE src/PluginEntry.cpp)
target_link_libraries(Custos PRIVATE custos_core juce::juce_recommended_config_flags)
# VST3-only plugin, never shipped as VST2 -> disable VST2 replacement so JUCE doesn't
# warn about VST2<->VST3 parameter-automation id mismatch (our ids are stable strings).
# Also disable JUCE's MIDI-CC-as-parameter emulation so the host sees exactly the
# kFacadeParamCount facade params, not 2080 extra "MIDI CC x/y" parameters.
target_compile_definitions(Custos PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)

enable_testing()
add_subdirectory(tests)
