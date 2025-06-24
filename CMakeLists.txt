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
  tests/test_interface.cpp
  tests/test_encrypted_link.cpp
  tests/TestUtils.cpp
  lib/lora_peer_link/src/RollCall.cpp
  lib/lora_peer_link/src/LoraBasicLink.cpp
  lib/lora_peer_link/src/LoraBackoffLink.cpp
  lib/lora_peer_link/src/EncryptedLoRaLink.cpp
  # Note: SemtechRadio.cpp excluded from tests as it requires external dependencies
  # Add other test_*.cpp files here as needed
)

# CLI tool for Python-C++ encryption cross-validation
add_executable(encryption_cli_tool
  tests/encryption_cli_tool.cpp
  tests/TestUtils.cpp
  lib/lora_peer_link/src/LoraBasicLink.cpp
  lib/lora_peer_link/src/EncryptedLoRaLink.cpp
)

set_target_properties(test_all PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

set_target_properties(encryption_cli_tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

target_link_libraries(test_all PRIVATE Catch2::Catch2WithMain)
target_include_directories(test_all PRIVATE ${catch2_SOURCE_DIR}/src)

target_include_directories(encryption_cli_tool PRIVATE 
  include
  lib/lora_peer_link/include
  tests
)

# Optional: Catch2 test discovery (for `ctest`)
include(CTest)
include(Catch)
catch_discover_tests(test_all)