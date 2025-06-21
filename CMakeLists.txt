cmake_minimum_required(VERSION 3.10)
project(LoRaPeerLinkTest)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.4.0  # Use the latest stable release
)

FetchContent_MakeAvailable(Catch2)

# Include your own headers
include_directories(
  include
  lib/lora_peer_link/include
)

# Test target
add_executable(test_simple_link
  tests/test_simple_link.cpp
)

# Correctly link Catch2 target and include directory
target_link_libraries(test_simple_link PRIVATE Catch2::Catch2WithMain)
target_include_directories(test_simple_link PRIVATE ${catch2_SOURCE_DIR}/src)

# Optional CTest integration
include(CTest)
include(Catch)
catch_discover_tests(test_simple_link)
