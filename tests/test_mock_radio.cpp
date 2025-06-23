#include <catch2/catch_test_macros.hpp>
#include "MockRadio.h"

TEST_CASE("MockRadio basic functionality", "[MockRadio]") {
    MockRadio radio;
    
    SECTION("MockRadio begins correctly") {
        REQUIRE(radio.begin() == true);
    }
    
    SECTION("MockRadio sends and receives data") {
        MockRadio::clearChannel();
        
        uint8_t data[] = {'h', 'e', 'l', 'l', 'o'};
        REQUIRE(radio.send(data, 5) == true);
        
        uint8_t buffer[10];
        int received = radio.receive(buffer, 10);
        REQUIRE(received == 5);
        REQUIRE(buffer[0] == 'h');
        REQUIRE(buffer[1] == 'e');
        REQUIRE(buffer[2] == 'l');
        REQUIRE(buffer[3] == 'l');
        REQUIRE(buffer[4] == 'o');
    }
    
    SECTION("MockRadio returns zero when no data available") {
        MockRadio::clearChannel();
        
        uint8_t buffer[10];
        int received = radio.receive(buffer, 10);
        REQUIRE(received == 0);
    }
}

TEST_CASE("MockRadio shared channel behavior", "[MockRadio]") {
    MockRadio radioA;
    MockRadio radioB;
    
    SECTION("Multiple radios share the same channel") {
        MockRadio::clearChannel();
        
        uint8_t dataA[] = {'A'};
        uint8_t dataB[] = {'B'};
        
        // RadioA sends first
        REQUIRE(radioA.send(dataA, 1) == true);
        
        // RadioB sends second
        REQUIRE(radioB.send(dataB, 1) == true);
        
        // Both radios should be able to receive messages in FIFO order
        uint8_t buffer[10];
        
        // First message sent (A) should be received first
        int received = radioA.receive(buffer, 10);
        REQUIRE(received == 1);
        REQUIRE(buffer[0] == 'A'); // First in, first out
        
        // Second message sent (B) should be received second
        received = radioB.receive(buffer, 10);
        REQUIRE(received == 1);
        REQUIRE(buffer[0] == 'B');
    }
    
    SECTION("Channel can be cleared") {
        uint8_t data[] = {'t', 'e', 's', 't'};
        radioA.send(data, 4);
        
        MockRadio::clearChannel();
        
        uint8_t buffer[10];
        int received = radioB.receive(buffer, 10);
        REQUIRE(received == 0);
    }
}

TEST_CASE("MockRadio buffer handling", "[MockRadio]") {
    MockRadio radio;
    MockRadio::clearChannel();
    
    SECTION("Handles buffer size limits") {
        uint8_t largeData[300];
        for (int i = 0; i < 300; i++) {
            largeData[i] = i & 0xFF;
        }
        
        // Send large data
        REQUIRE(radio.send(largeData, 300) == true);
        
        // Try to receive into small buffer
        uint8_t smallBuffer[10];
        int received = radio.receive(smallBuffer, 10);
        REQUIRE(received == 0); // Should reject if doesn't fit
    }
    
    SECTION("Handles exact buffer size") {
        uint8_t data[10];
        for (int i = 0; i < 10; i++) {
            data[i] = i;
        }
        
        REQUIRE(radio.send(data, 10) == true);
        
        uint8_t buffer[10];
        int received = radio.receive(buffer, 10);
        REQUIRE(received == 10);
        
        for (int i = 0; i < 10; i++) {
            REQUIRE(buffer[i] == i);
        }
    }
    
    SECTION("Handles empty data") {
        REQUIRE(radio.send(nullptr, 0) == true);
        
        uint8_t buffer[10];
        int received = radio.receive(buffer, 10);
        REQUIRE(received == 0);
    }
}

TEST_CASE("MockRadio RSSI and SNR", "[MockRadio]") {
    MockRadio radio;
    
    SECTION("Returns consistent RSSI and SNR values") {
        REQUIRE(radio.packetRssi() == -42);
        REQUIRE(radio.packetSnr() == 10.0f);
    }
}