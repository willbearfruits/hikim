cmake_minimum_required(VERSION 3.22)
project(DAWGLITCH VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- Locate JUCE -------------------------------------------------------------
# Resolution order:
#   1. -DJUCE_SOURCE_DIR=/path/to/JUCE on the cmake command line
#   2. a JUCE/ checkout next to this CMakeLists (git submodule)
#   3. /opt/JUCE (common system-wide install)
#   4. FetchContent, pinned to 8.0.13 (needs network on first configure)
if(DEFINED JUCE_SOURCE_DIR AND EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE EXCLUDE_FROM_ALL)
elseif(EXISTS "/opt/JUCE/CMakeLists.txt")
    add_subdirectory(/opt/JUCE "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.13
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(dawglitch
    PRODUCT_NAME "RUIN"
    COMPANY_NAME "characterglitch"
    BUNDLE_ID com.characterglitch.dawglitch
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "RUIN records audio."
    DOCUMENT_EXTENSIONS dgproj
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

juce_generate_juce_header(dawglitch)

target_sources(dawglitch PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Model/Session.cpp
    Source/Engine/TempoMap.cpp
    Source/Engine/Processors.cpp
    Source/Engine/ClipPlayer.cpp
    Source/Engine/MidiSource.cpp
    Source/Engine/AudioEngine.cpp
    Source/Engine/StretchCache.cpp
    Source/Engine/Renderer.cpp
    Source/Plugins/PluginHost.cpp
    Source/Rack/RackModules.cpp
    Source/Rack/RackProcessor.cpp
    Source/Rack/RackEditor.cpp
    Source/UI/Look.cpp
    Source/UI/TransportBar.cpp
    Source/UI/TimelineView.cpp
    Source/UI/PianoRoll.cpp
    Source/UI/MixerView.cpp
    Source/UI/Dialogs.cpp)

target_include_directories(dawglitch PRIVATE Source)

target_compile_definitions(dawglitch PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:dawglitch,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:dawglitch,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_AU=1            # only takes effect on macOS
    JUCE_ALSA=1
    JUCE_JACK=1
    # JUCE_ASIO=1                   # EXTEND: drop the ASIO SDK in and enable for low-latency Windows ASIO
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)   # GPLv3 build

# ---- RubberBand: pitch-locked time-stretch ----------------------------------
# System lib if present, else vendored single-file build (GPL, like this app).
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(RUBBERBAND IMPORTED_TARGET rubberband)
endif()
if(RUBBERBAND_FOUND)
    target_link_libraries(dawglitch PRIVATE PkgConfig::RUBBERBAND)
    target_compile_definitions(dawglitch PRIVATE DG_HAVE_RUBBERBAND=1)
else()
    include(FetchContent)
    FetchContent_Declare(rubberband
        GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
        GIT_TAG v3.3.0
        GIT_SHALLOW TRUE)
    # no CMakeLists upstream (meson project) - this just populates the sources
    FetchContent_MakeAvailable(rubberband)
    if(EXISTS "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp")
        target_sources(dawglitch PRIVATE "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp")
        target_include_directories(dawglitch PRIVATE "${rubberband_SOURCE_DIR}")
        target_compile_definitions(dawglitch PRIVATE DG_HAVE_RUBBERBAND=1)
    else()
        message(WARNING "RubberBand unavailable - pitch-locked stretch disabled (varispeed only)")
        target_compile_definitions(dawglitch PRIVATE DG_HAVE_RUBBERBAND=0)
    endif()
endif()

if(APPLE OR WIN32)
    target_compile_definitions(dawglitch PRIVATE DG_HAVE_VIDEO=1)
    target_link_libraries(dawglitch PRIVATE juce::juce_video)
else()
    # EXTEND: Linux video playback needs an external decoder (ffmpeg/libmpv).
    # The Video track + A/V sync logic is fully built; only the decoder is stubbed on Linux.
    target_compile_definitions(dawglitch PRIVATE DG_HAVE_VIDEO=0)
endif()

target_link_libraries(dawglitch PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
