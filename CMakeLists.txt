cmake_minimum_required(VERSION 3.22)

project(SubverseSplitter VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Fetch JUCE SDK
message(STATUS "Fetching JUCE Framework...")
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG develop
)
FetchContent_MakeAvailable(JUCE)

# Create the plugin target
juce_add_plugin(SubverseSplitter
    COMPANY_NAME "SubverseLab"
    PLUGIN_NAME "SubverseLabSplitter"
    PRODUCT_NAME "SubverseLabSplitter"
    VERSION "1.0.0"
    FORMATS Standalone VST3 AU
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    ICON_BIG "Resources/dock_icon.png"
)

juce_generate_juce_header(SubverseSplitter)

# Link ONNX Runtime directory
set(ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/_deps/onnxruntime")

if(WIN32)
    set(ONNXRUNTIME_LIB "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
    set(ONNXRUNTIME_DLL "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll")
else()
    set(ONNXRUNTIME_LIB "${ONNXRUNTIME_DIR}/lib/libonnxruntime.dylib")
endif()

# Binary Data
# Only the header mark. `background.png` and `loading_workers.png` were also
# compiled in and loaded into an Image at startup, and neither was ever drawn —
# 1.1 MB of AI-generated cartoon artwork carried by every build for nothing.
# The background is painted from Theme.h and the loading state is animated in
# code, so there was never anything for them to do.
juce_add_binary_data(SubverseSplitter_Assets
    SOURCES
        Resources/logo.png
)

# Add source files
target_sources(SubverseSplitter PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/Analysis.h
    Source/Analysis.cpp
    Source/Theme.h
)

target_link_libraries(SubverseSplitter PRIVATE SubverseSplitter_Assets)

target_include_directories(SubverseSplitter PRIVATE
    ${ONNXRUNTIME_DIR}/include
)

# Link JUCE core modules and ONNX Runtime
target_link_libraries(SubverseSplitter PRIVATE
    # juce_dsp carries the FFT and the windowing functions that Analysis.cpp
    # uses for the chroma and the onset envelope. It was not linked before
    # because nothing in the plugin analysed audio — only separated it.
    juce::juce_dsp
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_core
    ${ONNXRUNTIME_LIB}
)

if(WIN32)
    # Copy ONNX Runtime DLL and model to executable directory
    add_custom_command(TARGET SubverseSplitter_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${ONNXRUNTIME_DLL}" "$<TARGET_FILE_DIR:SubverseSplitter_Standalone>"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/Resources/htdemucs.onnx" "$<TARGET_FILE_DIR:SubverseSplitter_Standalone>"
    )
    add_custom_command(TARGET SubverseSplitter_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${ONNXRUNTIME_DLL}" "$<TARGET_FILE_DIR:SubverseSplitter_VST3>"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/Resources/htdemucs.onnx" "$<TARGET_FILE_DIR:SubverseSplitter_VST3>"
    )
endif()

# Copy the huge ONNX model next to the built binary to avoid 10-minute compilation times
# The model has to land inside each format's bundle, because that is where the
# plugin looks for it at runtime.
#
# This used to copy it next to the shared-code library instead — a directory no
# format ever searches — so every search path missed and separation failed for
# everyone. It went unnoticed because loadModel() ended with an absolute path
# into one developer's home directory, and on that one machine the fallback
# always hit. Nowhere else could it have worked.
#
# Three copies of a 302 MB file per build is not cheap. Copying it to fewer
# would mean shipping a format that cannot separate, which is the bug this
# replaces.
foreach(format Standalone VST3 AU)
    if(TARGET SubverseSplitter_${format})
        add_custom_command(TARGET SubverseSplitter_${format} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:SubverseSplitter_${format}>/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/Resources/htdemucs.onnx"
                "$<TARGET_BUNDLE_CONTENT_DIR:SubverseSplitter_${format}>/Resources/htdemucs.onnx"
            COMMENT "Copying htdemucs.onnx into the ${format} bundle"
        )
    endif()
endforeach()

# Optimization definitions
target_compile_definitions(SubverseSplitter PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# Test target to verify WAV writing functionality
add_executable(TestWavWriter test_wav_writer.cpp)
target_link_libraries(TestWavWriter PRIVATE
    juce::juce_audio_formats
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
)

# Test target to verify MP3 reading functionality
add_executable(TestModel test_model.cpp)
target_include_directories(TestModel PRIVATE ${ONNXRUNTIME_DIR}/include)
target_link_libraries(TestModel PRIVATE
    juce::juce_audio_formats
    juce::juce_core
    juce::juce_events
    ${ONNXRUNTIME_LIB}
)


# A console check for the tempo and key estimator, kept out of the plugin
# target. Verified against the web Splitter's reading of the same file rather
# than against its own output.
juce_add_console_app(test_analysis PRODUCT_NAME "test_analysis")
juce_generate_juce_header(test_analysis)
target_sources(test_analysis PRIVATE test_analysis.cpp Source/Analysis.cpp)
target_compile_definitions(test_analysis PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(test_analysis PRIVATE
    juce::juce_dsp
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_core
)


# The window plan, checked without the model. Header-only, so this needs no
# JUCE and no ONNX Runtime.
add_executable(test_overlap test_overlap.cpp)
target_include_directories(test_overlap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


# Renders the editor offscreen so its layout can be looked at without a screen
# and without Screen Recording permission. Links the plugin's shared code.
juce_add_console_app(test_ui_snapshot PRODUCT_NAME "test_ui_snapshot")
juce_generate_juce_header(test_ui_snapshot)
target_sources(test_ui_snapshot PRIVATE
    test_ui_snapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Analysis.cpp
)
target_include_directories(test_ui_snapshot PRIVATE ${ONNXRUNTIME_DIR}/include)
target_compile_definitions(test_ui_snapshot PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="SubverseLabSplitter"
    JucePlugin_IsSynth=0 JucePlugin_WantsMidiInput=0 JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
)
target_link_libraries(test_ui_snapshot PRIVATE
    SubverseSplitter_Assets
    juce::juce_dsp juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_gui_extra juce::juce_gui_basics juce::juce_graphics
    juce::juce_events juce::juce_core
    ${ONNXRUNTIME_LIB}
)
