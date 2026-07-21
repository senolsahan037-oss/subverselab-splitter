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
juce_add_binary_data(SubverseSplitter_Assets
    SOURCES
        Resources/logo.png
        Resources/background.png
        Resources/loading_workers.png
)

# Add source files
target_sources(SubverseSplitter PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
)

target_link_libraries(SubverseSplitter PRIVATE SubverseSplitter_Assets)

target_include_directories(SubverseSplitter PRIVATE
    ${ONNXRUNTIME_DIR}/include
)

# Link JUCE core modules and ONNX Runtime
target_link_libraries(SubverseSplitter PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_core
    ${ONNXRUNTIME_LIB}
)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
