cmake_minimum_required(VERSION 3.20)

add_executable(audio-io-cli
    audio-io-cli.cpp
)

target_link_libraries(audio-io-cli
    PRIVATE
        audio-io
)

target_link_directories(audio-io-cli
    PRIVATE
        ${CMAKE_BINARY_DIR}/Release
)

install(TARGETS audio-io-cli
    RUNTIME DESTINATION bin/tools
)
