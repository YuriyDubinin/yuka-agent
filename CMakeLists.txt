cmake_minimum_required(VERSION 3.15)
project(yuka_agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------
include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(
    nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)
FetchContent_MakeAvailable(nlohmann_json)

# ------------------------------------------------------------
# Executable
# ------------------------------------------------------------
add_executable(yuka-agent
    src/app/main.cpp
    src/agent/config.cpp
    src/llm/ollama_client.cpp
    src/faces/face_discovery.cpp
    src/faces/terminal/terminal_face.cpp
    src/faces/socket/socket_face.cpp
    src/platform/signals.cpp
)

# Project headers are included relative to src/, e.g. "faces/socket/socket_face.hpp".
target_include_directories(yuka-agent PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(yuka-agent PRIVATE nlohmann_json::nlohmann_json)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(yuka-agent PRIVATE -Wall -Wextra -Wpedantic)
endif()
