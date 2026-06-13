cmake_minimum_required(VERSION 3.22)
project(DAWGLITCH VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Static CRT on MSVC so the installed build is plug-and-play (no VC++ redist
# needed - HIKIM.exe + ffmpeg.exe is the whole runtime). Must be set before any
# target (JUCE/RubberBand/mimalloc inherit it).
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
    PRODUCT_NAME "HIKIM"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "characterglitch"
    BUNDLE_ID com.characterglitch.dawglitch
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.willbearfruits.hikim.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "HIKIM records audio."
    DOCUMENT_EXTENSIONS dgproj
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

juce_generate_juce_header(dawglitch)

target_sources(dawglitch PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Model/Session.cpp
    Source/Model/ClipOps.cpp
    Source/Model/AutoGen.cpp
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
    Source/Patcher/PatcherProcessor.cpp
    Source/Patcher/PatcherEditor.cpp
    Source/UI/Look.cpp
    Source/UI/NodeCanvas.cpp
    Source/UI/TransportBar.cpp
    Source/UI/TimelineView.cpp
    Source/UI/PianoRoll.cpp
    Source/UI/MixerView.cpp
    Source/UI/BrowserPanel.cpp
    Source/UI/ChainPanel.cpp
    Source/UI/SessionGrid.cpp
    Source/UI/SampleEditor.cpp
    Source/UI/Dialogs.cpp)

target_include_directories(dawglitch PRIVATE Source)

target_compile_definitions(dawglitch PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:dawglitch,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:dawglitch,JUCE_VERSION>"
    JUCE_USE_MP3AUDIOFORMAT=1       # mp3 decode on Linux (mac/win get it via OS codecs)
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_AU=1            # only takes effect on macOS
    # JUCE_ASIO=1                   # EXTEND: drop the ASIO SDK in and enable for low-latency Windows ASIO
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)   # AGPLv3 build (JUCE 8 free tier)

if(UNIX AND NOT APPLE)
    # curl is dlopened at runtime (every distro + the flatpak runtime ship it);
    # the update check needs working URL streams, which Linux routes through curl
    target_compile_definitions(dawglitch PRIVATE JUCE_ALSA=1 JUCE_JACK=1
        JUCE_USE_CURL=1 JUCE_LOAD_CURL_SYMBOLS_LAZILY=1)
else()
    target_compile_definitions(dawglitch PRIVATE JUCE_USE_CURL=0)   # Windows/mac use native web streams
endif()

# MSVC: windows.h's min/max macros break std::numeric_limits<>::max() in
# RubberBand (C2589) - NOMINMAX is required for the vendored single-file build.
if(MSVC)
    target_compile_definitions(dawglitch PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

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

# ---- mimalloc: a faster allocator for the message/disk threads. The RT audio
# thread pre-allocates and never mallocs, so this is purely a non-realtime
# throughput win (edit responsiveness, snapshot builds, load times) and is
# bit-neutral. We override only C++ new/delete (via mimalloc-new-delete.h in
# Main.cpp), not the C malloc redirect, which keeps integration trivial and
# identical on Windows/Linux/macOS. Opt out with -DHIKIM_USE_MIMALLOC=OFF.
option(HIKIM_USE_MIMALLOC "Use mimalloc for the non-RT allocator" ON)
if(HIKIM_USE_MIMALLOC)
    # System-first then FetchContent, matching the JUCE/RubberBand pattern - a
    # distro-provided mimalloc (or an offline box with one installed) is used
    # without any network fetch.
    find_package(mimalloc QUIET)
    if(mimalloc_FOUND)
        # imported target is `mimalloc-static` (or `mimalloc`); prefer static
        if(TARGET mimalloc-static)
            target_link_libraries(dawglitch PRIVATE mimalloc-static)
        else()
            target_link_libraries(dawglitch PRIVATE mimalloc)
        endif()
        target_compile_definitions(dawglitch PRIVATE DG_USE_MIMALLOC=1)
    else()
        include(FetchContent)
        FetchContent_Declare(mimalloc
            GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
            GIT_TAG v2.1.7
            GIT_SHALLOW TRUE)
        set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
        set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
        set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(mimalloc)
        target_link_libraries(dawglitch PRIVATE mimalloc-static)
        target_include_directories(dawglitch PRIVATE "${mimalloc_SOURCE_DIR}/include")
        target_compile_definitions(dawglitch PRIVATE DG_USE_MIMALLOC=1)
    endif()
endif()

# avrt (MMCSS, used by the realtime audio-thread priority on Windows) - owned by
# the build system so it links on every Windows toolchain, not just MSVC's pragma
if(WIN32)
    target_link_libraries(dawglitch PRIVATE avrt)
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
    juce::juce_osc
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

# ---- headless test suite -----------------------------------------------------
# cmake --build build --target ruin_tests && ./build/ruin_tests_artefacts/ruin-tests
juce_add_console_app(ruin_tests PRODUCT_NAME "ruin-tests")
juce_generate_juce_header(ruin_tests)
target_sources(ruin_tests PRIVATE
    Tests/Tests.cpp
    Source/Model/Session.cpp
    Source/Model/ClipOps.cpp
    Source/Model/AutoGen.cpp
    Source/Engine/TempoMap.cpp
    Source/Engine/Processors.cpp
    Source/Engine/StretchCache.cpp
    Source/Rack/RackModules.cpp
    Source/Rack/RackProcessor.cpp
    Source/Rack/RackEditor.cpp
    Source/Patcher/PatcherProcessor.cpp
    Source/Patcher/PatcherEditor.cpp
    Source/UI/Look.cpp
    Source/UI/NodeCanvas.cpp)
target_include_directories(ruin_tests PRIVATE Source)
target_compile_definitions(ruin_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)
if(MSVC)
    target_compile_definitions(ruin_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(RUBBERBAND_FOUND)
    target_link_libraries(ruin_tests PRIVATE PkgConfig::RUBBERBAND)
    target_compile_definitions(ruin_tests PRIVATE DG_HAVE_RUBBERBAND=1)
elseif(EXISTS "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp")
    target_sources(ruin_tests PRIVATE "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp")
    target_include_directories(ruin_tests PRIVATE "${rubberband_SOURCE_DIR}")
    target_compile_definitions(ruin_tests PRIVATE DG_HAVE_RUBBERBAND=1)
else()
    target_compile_definitions(ruin_tests PRIVATE DG_HAVE_RUBBERBAND=0)
endif()
target_link_libraries(ruin_tests PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_osc
    juce::juce_recommended_config_flags)
