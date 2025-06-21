cmake_minimum_required(VERSION 3.10)
project(LoRaPeerLinkTest)

set(CMAKE_CXX_STANDARD 17)

include_directories(include lib/lora_peer_link/include)

add_executable(test_simple_link
    tests/test_simple_link.cpp
)

# Link Catch2 if not header-only
find_package(Catch2 REQUIRED)
target_link_libraries(test_simple_link Catch2::Catch2)
