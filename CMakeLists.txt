cmake_minimum_required(VERSION 3.22)
project(Demiurge VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DEMIURGE_BUILD_TESTS "Build the engine unit tests" ON)
option(DEMIURGE_BUILD_CLAP "Build the CLAP plugin (via clap-juce-extensions)" ON)
option(DEMIURGE_BUILD_SNAPSHOT "Build the UI snapshot tool" OFF)
option(DEMIURGE_COPY_AFTER_BUILD "Copy the built plugins into the system plugin folders" ON)
set(DEMIURGE_CLAP_COPY_DIR "" CACHE PATH "Where to copy the CLAP after a build (empty = system default)")

# Point JUCE_DIR at a local JUCE checkout, or leave it empty to download JUCE.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# CLAP comes from clap-juce-extensions (JUCE 9.0 has no native CLAP format). Point
# CLAP_JUCE_EXTENSIONS_DIR at a local checkout (with submodules), or leave it empty to download it.
if(DEMIURGE_BUILD_CLAP)
    set(CLAP_JUCE_EXTENSIONS_DIR "" CACHE PATH "Path to a local clap-juce-extensions checkout")
    if(CLAP_JUCE_EXTENSIONS_DIR)
        add_subdirectory(${CLAP_JUCE_EXTENSIONS_DIR} clap-juce-extensions EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(clap-juce-extensions
            GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
            GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
            GIT_SUBMODULES_RECURSE TRUE)
        FetchContent_MakeAvailable(clap-juce-extensions)
    endif()
endif()

# The Inter typeface (SIL Open Font License, see fonts/OFL.txt), shared by the Bidoo plugins
# through BidooLook.h
juce_add_binary_data(DemiurgeFonts
    HEADER_NAME BidooFonts.h
    NAMESPACE BidooFonts
    SOURCES fonts/Inter-Medium.otf fonts/Inter-SemiBold.otf)
set_target_properties(DemiurgeFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The song model, player and the Claude requests: JUCE core only, so they can be tested headless.
set(DEMIURGE_ENGINE_SOURCES
    src/engine/Song.cpp
    src/engine/Player.cpp
    src/engine/Composer.cpp)

set(DEMIURGE_PLUGIN_SOURCES
    ${DEMIURGE_ENGINE_SOURCES}
    src/plugin/AiClient.cpp
    src/plugin/Exchange.cpp
    src/plugin/PluginProcessor.cpp
    src/ui/ArrangementView.cpp
    src/ui/PianoRoll.cpp
    src/ui/PluginEditor.cpp)

# HTTPS: WinINet on Windows, NSURLSession on macOS, libcurl on Linux (NEEDS_CURL below).
if(UNIX AND NOT APPLE)
    set(DEMIURGE_NEEDS_CURL TRUE)
else()
    set(DEMIURGE_NEEDS_CURL FALSE)
endif()

if(DEMIURGE_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(engine_tests PRODUCT_NAME "engine_tests")
    target_sources(engine_tests PRIVATE tests/EngineTests.cpp ${DEMIURGE_ENGINE_SOURCES} src/plugin/Exchange.cpp)
    target_include_directories(engine_tests PRIVATE src)
    target_compile_definitions(engine_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(engine_tests PRIVATE juce::juce_core juce::juce_data_structures juce::juce_audio_basics)
    add_test(NAME engine_tests COMMAND engine_tests)
endif()

# Formats: VST3 and Standalone everywhere, plus AU on macOS. CLAP is added below.
set(DEMIURGE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DEMIURGE_FORMATS AU)
endif()

function(demiurge_setup target)
    target_sources(${target} PRIVATE ${DEMIURGE_PLUGIN_SOURCES})
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_LOAD_CURL_SYMBOLS_LAZILY=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_include_directories(${target} PRIVATE src src/ui)
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_link_libraries(${target}
        PRIVATE
            DemiurgeFonts
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

# The main plugin: an effect with audio pass-through that outputs MIDI. This is what
# Reaper, Bitwig, Cubase and most VST3 hosts expect from a MIDI generator.
juce_add_plugin(Demiurge
    COMPANY_NAME "Bidoo"
    PLUGIN_MANUFACTURER_CODE Bido
    PLUGIN_CODE Dmrg
    PRODUCT_NAME "Demiurge"
    FORMATS ${DEMIURGE_FORMATS}
    IS_SYNTH FALSE
    IS_MIDI_EFFECT FALSE      # stays a normal FX with audio pass-through: Reaper routes its MIDI out
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    NEEDS_CURL ${DEMIURGE_NEEDS_CURL}
    VST3_CATEGORIES Fx Generator
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    BUNDLE_ID com.bidoo.demiurge
    COPY_PLUGIN_AFTER_BUILD ${DEMIURGE_COPY_AFTER_BUILD})
demiurge_setup(Demiurge)

if(DEMIURGE_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET Demiurge
        CLAP_ID "com.bidoo.demiurge"
        CLAP_FEATURES note-effect utility)
    add_dependencies(Demiurge_All Demiurge_CLAP)

    # clap-juce-extensions copies the .clap itself on macOS and Linux but not on Windows.
    set(clap_copy_dir "${DEMIURGE_CLAP_COPY_DIR}")
    if(NOT clap_copy_dir AND WIN32)
        set(clap_copy_dir "C:/Program Files/Common Files/CLAP")
    endif()
    if(DEMIURGE_COPY_AFTER_BUILD AND clap_copy_dir)
        file(TO_CMAKE_PATH "${clap_copy_dir}" clap_copy_dir)
        if(APPLE)
            set(clap_copy_cmd copy_directory "$<TARGET_BUNDLE_DIR:Demiurge_CLAP>"
                "${clap_copy_dir}/$<TARGET_BUNDLE_DIR_NAME:Demiurge_CLAP>")
        else()
            set(clap_copy_cmd copy_if_different "$<TARGET_FILE:Demiurge_CLAP>" "${clap_copy_dir}")
        endif()
        add_custom_command(TARGET Demiurge_CLAP POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copying Demiurge.clap to ${clap_copy_dir}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${clap_copy_dir}"
            COMMAND ${CMAKE_COMMAND} -E ${clap_copy_cmd}
            VERBATIM)
    endif()
endif()

# Windows: the plugin folders get every build's VST3 and CLAP, even when nothing was relinked
# (switching between Debug and Release).
if(DEMIURGE_COPY_AFTER_BUILD)
    include(cmake/KeepPluginsInstalled.cmake)
    keep_plugins_installed(NAME Demiurge TARGETS Demiurge_VST3 Demiurge_CLAP CLAP_DIR "${clap_copy_dir}")
endif()

# macOS: a second AU built as a MIDI FX (type aumi), the only kind of AU whose MIDI output
# Logic Pro and GarageBand use. Load it in a track's MIDI FX slot.
if(APPLE)
    juce_add_plugin(DemiurgeFX
        COMPANY_NAME "Bidoo"
        PLUGIN_MANUFACTURER_CODE Bido
        PLUGIN_CODE Dmrf
        PRODUCT_NAME "Demiurge FX"
        FORMATS AU
        IS_SYNTH FALSE
        IS_MIDI_EFFECT TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        BUNDLE_ID com.bidoo.demiurge.fx
        COPY_PLUGIN_AFTER_BUILD ${DEMIURGE_COPY_AFTER_BUILD})
    demiurge_setup(DemiurgeFX)
endif()

# A host-less app that renders the editor to PNGs (see tools/Snapshot.cpp).
if(DEMIURGE_BUILD_SNAPSHOT)
    juce_add_console_app(ui_snapshot PRODUCT_NAME "ui_snapshot" NEEDS_CURL ${DEMIURGE_NEEDS_CURL})
    target_sources(ui_snapshot PRIVATE tools/Snapshot.cpp ${DEMIURGE_PLUGIN_SOURCES})
    target_include_directories(ui_snapshot PRIVATE src src/ui)
    target_compile_definitions(ui_snapshot PRIVATE
        JucePlugin_Name="Demiurge" JucePlugin_IsMidiEffect=0 JucePlugin_Manufacturer="Bidoo"
        JUCE_WEB_BROWSER=0 JUCE_LOAD_CURL_SYMBOLS_LAZILY=1
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(ui_snapshot PRIVATE DemiurgeFonts juce::juce_audio_utils)
endif()
