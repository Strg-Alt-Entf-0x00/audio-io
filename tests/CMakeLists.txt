cmake_minimum_required(VERSION 3.20)

# WAV decoder test
add_executable(audio-io-test
    test_wave_decoder.cpp
)

# MP3 encoder test
add_executable(audio-io-mp3-test
    test_mp3_encoder.cpp
)

# Streaming and roundtrip tests
add_executable(audio-io-streaming-test
    test_streaming.cpp
)

# Benchmark tool
add_executable(audio-io-benchmark
    benchmark_codec.cpp
)

target_link_libraries(audio-io-test
    PRIVATE
        audio-io
)

target_link_libraries(audio-io-mp3-test
    PRIVATE
        audio-io
)

target_link_libraries(audio-io-streaming-test
    PRIVATE
        audio-io
)

target_link_libraries(audio-io-benchmark
    PRIVATE
        audio-io
)

# Set correct path to find audio-io.lib
target_link_directories(audio-io-test
    PRIVATE
        ${CMAKE_BINARY_DIR}/Release
)

target_link_directories(audio-io-mp3-test
    PRIVATE
        ${CMAKE_BINARY_DIR}/Release
)

target_link_directories(audio-io-streaming-test
    PRIVATE
        ${CMAKE_BINARY_DIR}/Release
)

target_link_directories(audio-io-benchmark
    PRIVATE
        ${CMAKE_BINARY_DIR}/Release
)

# Copy test files to build directory
file(GLOB TEST_AUDIO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/test_files/*.wav")
file(COPY ${TEST_AUDIO_FILES} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/test_files)

# Installation
install(TARGETS audio-io-test audio-io-mp3-test audio-io-streaming-test
    RUNTIME DESTINATION bin/tests
)
