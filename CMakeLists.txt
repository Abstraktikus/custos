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
    src/CustosProcessor.cpp)
target_include_directories(custos_core PUBLIC src)
target_compile_features(custos_core PUBLIC cxx_std_17)
target_compile_definitions(custos_core PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)
target_link_libraries(custos_core
    PUBLIC
        juce::juce_audio_processors
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
if(CUSTOS_HARDCODED_SYNTH_PATH)
    target_compile_definitions(custos_core PUBLIC
        CUSTOS_HARDCODED_SYNTH_PATH="${CUSTOS_HARDCODED_SYNTH_PATH}")
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
    VST3_CATEGORIES "Instrument"
    COPY_PLUGIN_STEP FALSE)
target_sources(Custos PRIVATE src/PluginEntry.cpp)
target_link_libraries(Custos PRIVATE custos_core juce::juce_recommended_config_flags)

enable_testing()
add_subdirectory(tests)
