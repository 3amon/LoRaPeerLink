# Simple Makefile for LoRaPeerLink examples
# Usage: make basic_example

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -I include -I tests
SRCDIR = src
SOURCES = $(SRCDIR)/LoraBasicLink.cpp

# Targets
.PHONY: all clean test

all: basic_example peer_messaging_example

basic_example: examples/basic_communication.cpp $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^

encryption_example: examples/encryption_example.cpp $(SOURCES) $(SRCDIR)/EncryptedLoRaLink.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

peer_messaging_example: examples/peer_messaging_example.cpp $(SOURCES) $(SRCDIR)/RollCall.cpp $(SRCDIR)/PeerMessenger.cpp tests/TestUtils.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test:
	@echo "Running library tests..."
	@cd build && make && ./tests/test_all

clean:
	rm -f basic_example encryption_example peer_messaging_example

help:
	@echo "Available targets:"
	@echo "  basic_example          - Build basic communication example"
	@echo "  encryption_example     - Build encryption example"
	@echo "  peer_messaging_example - Build PeerMessenger example"
	@echo "  test                   - Run library tests"
	@echo "  clean                  - Remove built examples"
	@echo "  help                   - Show this help"