include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.15.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(custos_tests
    ${CMAKE_SOURCE_DIR}/src/CustosProcessor.cpp
    FacadeParameterTest.cpp
    InnerBindingTest.cpp
    PassthroughTest.cpp
    SynthLoaderTest.cpp
    EditorTest.cpp
    SwapTest.cpp
    StateCodecTest.cpp
    OscCommandTest.cpp
    OscContractTest.cpp
    AddressingTest.cpp
    ParamDumpTest.cpp
    VolumeTest.cpp
    FavoritesStoreTest.cpp
    FavoritesTest.cpp
    MidiRouteTest.cpp)
target_include_directories(custos_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(custos_tests PRIVATE custos_core Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(custos_tests)
