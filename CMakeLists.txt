cmake_minimum_required(VERSION 3.22)
project(ARCO VERSION 1.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
if(NOT DEFINED ARCO_BUILD)
  set(ARCO_BUILD 0)
endif()
if(NOT DEFINED JUCE_DIR)
  set(JUCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/JUCE)
endif()
add_subdirectory(${JUCE_DIR} juce)

juce_add_gui_app(ARCO
    PRODUCT_NAME "ARCO"
    COMPANY_NAME "ARCO"
    BUNDLE_ID "com.arco.station"
    VERSION 1.1.0
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ARCO records your instrument from your sound card."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input)

juce_generate_juce_header(ARCO)
juce_add_binary_data(ARCOData SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Inter-Regular.otf
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Inter-SemiBold.otf
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Inter-Bold.otf)

target_sources(ARCO PRIVATE
    Source/Main.cpp
    Source/Engine/Engine.cpp
    Source/Model/Model.cpp
    Source/App/AudioHost.cpp
    Source/App/PhoneLink.cpp
    $<$<PLATFORM_ID:iOS>:Source/App/IOSAudio.mm>
    Source/UI/MainView.cpp
    Source/UI/Panels.cpp
    Source/UI/SynthPanel.cpp)

target_compile_definitions(ARCO PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ARCO,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ARCO,JUCE_VERSION>" JUCE_DISPLAY_SPLASH_SCREEN=0 ARCO_BUILD=${ARCO_BUILD})
if(MSVC)
else()
  target_compile_options(ARCO PRIVATE -O3 -ffast-math -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-parameter)
endif()
target_link_libraries(ARCO PRIVATE ARCOData
    juce::juce_audio_utils juce::juce_audio_devices juce::juce_audio_formats juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
