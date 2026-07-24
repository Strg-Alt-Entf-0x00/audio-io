cmake_minimum_required(VERSION 3.20)
project(audio-io VERSION 1.0.0 LANGUAGES CXX)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Configuration banner
message(STATUS "")
message(STATUS "========================================")
message(STATUS "audio-io Configuration")
message(STATUS "========================================")
message(STATUS "Version:        ${PROJECT_VERSION}")
message(STATUS "Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:   C++${CMAKE_CXX_STANDARD}")
if(MSVC)
    message(STATUS "Compiler:       MSVC ${MSVC_VERSION}")
else()
    message(STATUS "Compiler:       ${CMAKE_CXX_COMPILER_ID}")
endif()
message(STATUS "Codecs:         WAV, MP3 (decode + encode)")
message(STATUS "Features:       Resampling (Kaiser-Sinc), Channel Mix")
message(STATUS "Metadata:       ID3v2 (MP3), RIFF INFO (WAV), Cover Art")
message(STATUS "========================================")
message(STATUS "")

# Windows-specific flags
if(MSVC)
    add_compile_options(
        /W4                     # Warning level 4
        /permissive-            # Standards conformance
        /Zc:__cplusplus         # Correct __cplusplus macro
        /utf-8                  # UTF-8 source files
        /MP                     # Multi-processor compilation
        $<$<CONFIG:Release>:/O2>     # Optimize for speed
        $<$<CONFIG:Release>:/GL>     # Whole program optimization
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>   # Link-time code generation
    )
endif()

# audio-io library (SHARED)
add_library(audio-io SHARED
    # Public API
    include/audio_io/audio_format.h
    include/audio_io/audio_reader.h
    include/audio_io/audio_writer.h
    include/audio_io/audio_metadata.h
    include/audio_io/audio_stream.h
    include/audio_io/expected.h
    include/audio_io/export.h
    src/audio_reader.cpp
    src/audio_writer.cpp
    src/audio_stream.cpp
    src/io/file_reader.cpp
    
    # Core DSP
    src/resampler/sinc_resampler.h
    src/resampler/sinc_resampler.cpp
    
    # Metadata (readers + writers)
    src/metadata/id3v2_reader.h
    src/metadata/id3v2_reader.cpp
    src/metadata/id3v2_writer.h
    src/metadata/id3v2_writer.cpp
    src/metadata/riff_metadata_reader.h
    src/metadata/riff_metadata_reader.cpp
    src/metadata/riff_info_writer.h
    src/metadata/riff_info_writer.cpp
    
    # WAV decoder
    src/wave/wave_decoder.h
    src/wave/wave_decoder.cpp
    
    # MP3 codec (encode + decode)
    src/mp3/mp3_codec.h
    src/mp3/mp3_codec.cpp
    src/mp3/mpeg_layer3_decoder_native.h
    
    # MP3 encoder
    src/mp3/encoder/mp3enc.h
    src/mp3/encoder/mp3enc-psy.h
    src/mp3/encoder/mp3enc-tables.h
    src/mp3/encoder/mp3enc-quant.h
    src/mp3/encoder/mp3enc-bits.h
    src/mp3/encoder/mp3enc-filter.h
    src/mp3/encoder/mp3enc-mdct.h
    src/mp3/encoder/mp3enc-huff.h
    src/mp3/encoder/tabs_data.h
)

target_include_directories(audio-io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Windows libraries
target_link_libraries(audio-io
    PRIVATE
        kernel32    # CreateFileW, ReadFile, etc.
)

# Export symbols (PROFESSIONAL WAY)
target_compile_definitions(audio-io
    PRIVATE
        AUDIO_IO_BUILDING_DLL
)

# Install rules
install(TARGETS audio-io
    EXPORT audio-io-targets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
)

# Export cmake targets
install(EXPORT audio-io-targets
    FILE audio-io-targets.cmake
    NAMESPACE audio-io::
    DESTINATION lib/cmake/audio-io
)

# Package config
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/audio-io-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/audio-io-config.cmake
    INSTALL_DESTINATION lib/cmake/audio-io
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/audio-io-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/audio-io-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/audio-io-config-version.cmake
    DESTINATION lib/cmake/audio-io
)

# Optional: Build tests
option(AUDIO_IO_BUILD_TESTS "Build audio-io tests" ON)

if(AUDIO_IO_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Build tools
add_subdirectory(tools)
