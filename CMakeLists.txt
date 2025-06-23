cmake_minimum_required(VERSION 3.10)
project(LoRaPeerLinkTest)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.4.0
)

FetchContent_MakeAvailable(Catch2)

# Include your headers
include_directories(
  include
  lib/lora_peer_link/include
)

# Single test executable with all suites
add_executable(test_all
  tests/test_simple_link.cpp
  tests/test_backoff_link.cpp
  tests/test_mock_radio.cpp
  tests/test_crc.cpp  
  tests/test_packet_framing.cpp
  tests/test_backoff_packet_framing.cpp
  tests/test_rollcall.cpp
  tests/TestUtils.cpp
  lib/lora_peer_link/src/RollCall.cpp
  # Add other test_*.cpp files here as needed
)

set_target_properties(test_all PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

target_link_libraries(test_all PRIVATE Catch2::Catch2WithMain)
target_include_directories(test_all PRIVATE ${catch2_SOURCE_DIR}/src)

# Optional: Catch2 test discovery (for `ctest`)
include(CTest)
include(Catch)
catch_discover_tests(test_all)