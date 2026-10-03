# SCORE - the composition app (the second app in this repository; it is built next to ARCO,
# or on its own - that is how the iPad build is made:  cmake -S Score -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.22)
  project(SCORE)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../JUCE)
  endif()
  add_subdirectory(${JUCE_DIR} juce)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(SCORE_ICON ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon-ios.png)
else()
  set(SCORE_ICON ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png)
endif()
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/VERSION SCORE_VERSION)
string(STRIP ${SCORE_VERSION} SCORE_VERSION)

juce_add_gui_app(SCORE
    PRODUCT_NAME "SCORE"
    COMPANY_NAME "ARCO"
    BUNDLE_ID "com.pacello.score"
    VERSION ${SCORE_VERSION}
    ICON_BIG ${SCORE_ICON}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "SCORE records your instrument from your sound card or microphone."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input
    # iPad / iPhone
    FILE_SHARING_ENABLED TRUE
    DOCUMENT_BROWSER_ENABLED TRUE
    BACKGROUND_AUDIO_ENABLED TRUE
    REQUIRES_FULL_SCREEN TRUE
    STATUS_BAR_HIDDEN TRUE
    TARGETED_DEVICE_FAMILY "1,2"
    IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
    IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight)

juce_generate_juce_header(SCORE)
juce_add_binary_data(ScoreData NAMESPACE ScoreBinary SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../Resources/Inter-Regular.otf
    ${CMAKE_CURRENT_SOURCE_DIR}/../Resources/Inter-SemiBold.otf
    ${CMAKE_CURRENT_SOURCE_DIR}/../Resources/Inter-Bold.otf
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png)

file(GLOB SCORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/*.cpp)
target_sources(SCORE PRIVATE ${SCORE_SOURCES})
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  target_sources(SCORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/IOSAudio.mm)
  set_target_properties(SCORE PROPERTIES XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0")
endif()

if(NOT DEFINED ARCO_BUILD)
  set(ARCO_BUILD 0)
endif()
target_compile_definitions(SCORE PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SCORE,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SCORE,JUCE_VERSION>"
    SCORE_BUILD=${ARCO_BUILD})
if(NOT MSVC)
  target_compile_options(SCORE PRIVATE -O3 -ffast-math -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-parameter -Wno-misleading-indentation)
endif()
target_link_libraries(SCORE PRIVATE ScoreData
    juce::juce_audio_utils juce::juce_audio_devices juce::juce_audio_formats juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
