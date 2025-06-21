cmake_minimum_required(VERSION 3.10)
project(LoRaPeerLinkTest)

set(CMAKE_CXX_STANDARD 17)

# Pull Catch2 automatically
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.4.0  # Use the latest stable or desired tag
)
FetchContent_MakeAvailable(Catch2)

# Include your project's headers
include_directories(
  include
  lib/lora_peer_link/include
)

# Define test executable
add_executable(test_simple_link
  tests/test_simple_link.cpp
)

# Link Catch2 test runner
target_link_libraries(test_simple_link Catch2::Catch2WithMain)

# Enable CTest integration (optional)
include(CTest)
include(Catch)
catch_discover_tests(test_simple_link)
