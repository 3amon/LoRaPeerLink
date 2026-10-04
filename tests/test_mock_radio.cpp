#include <catch2/catch_test_macros.hpp>
#include "MockRadio.h"

TEST_CASE("MockRadio basic functionality", "[MockRadio]") {
    MockRadio sender;
    MockRadio receiver;
    MockRadio::clearChannel();

    SECTION("MockRadio begins correctly") {
        REQUIRE(sender.begin() == true);
    }

    SECTION("A packet sent by one radio is received by another") {
        uint8_t data[] = {'h', 'e', 'l', 'l', 'o'};
        REQUIRE(sender.send(data, 5) == true);
        REQUIRE(sender.sentCount == 1);

        uint8_t buffer[10];
        int received = receiver.receive(buffer, 10);
        REQUIRE(received == 5);
        REQUIRE(buffer[0] == 'h');
        REQUIRE(buffer[1] == 'e');
        REQUIRE(buffer[2] == 'l');
        REQUIRE(buffer[3] == 'l');
        REQUIRE(buffer[4] == 'o');

        // Delivered once.
        REQUIRE(receiver.receive(buffer, 10) == 0);
    }

    SECTION("A radio never hears its own transmission") {
        uint8_t data[] = {'x'};
        REQUIRE(sender.send(data, 1) == true);
        uint8_t buffer[10];
        REQUIRE(sender.receive(buffer, 10) == 0);
        REQUIRE(sender.pending() == 0);
        REQUIRE(receiver.pending() == 1);
    }

    SECTION("MockRadio returns zero when no data available") {
        uint8_t buffer[10];
        REQUIRE(receiver.receive(buffer, 10) == 0);
    }
}

TEST_CASE("MockRadio shared channel behavior", "[MockRadio]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio radioC;
    MockRadio::clearChannel();

    SECTION("Every other radio gets its own copy, in the order sent") {
        uint8_t dataA[] = {'A'};
        uint8_t dataB[] = {'B'};
        REQUIRE(radioA.send(dataA, 1) == true);
        REQUIRE(radioB.send(dataB, 1) == true);

        uint8_t buffer[10];
        // C heard both, A first.
        REQUIRE(radioC.receive(buffer, 10) == 1);
        REQUIRE(buffer[0] == 'A');
        REQUIRE(radioC.receive(buffer, 10) == 1);
        REQUIRE(buffer[0] == 'B');
        REQUIRE(radioC.receive(buffer, 10) == 0);

        // A heard only B, and B heard only A.
        REQUIRE(radioA.receive(buffer, 10) == 1);
        REQUIRE(buffer[0] == 'B');
        REQUIRE(radioA.receive(buffer, 10) == 0);
        REQUIRE(radioB.receive(buffer, 10) == 1);
        REQUIRE(buffer[0] == 'A');
        REQUIRE(radioB.receive(buffer, 10) == 0);
    }

    SECTION("Channel can be cleared") {
        uint8_t data[] = {'t', 'e', 's', 't'};
        radioA.send(data, 4);

        MockRadio::clearChannel();

        uint8_t buffer[10];
        REQUIRE(radioB.receive(buffer, 10) == 0);
        REQUIRE(radioC.receive(buffer, 10) == 0);
    }

    SECTION("A radio that has been destroyed no longer receives") {
        {
            MockRadio temporary;
            uint8_t data[] = {'1'};
            radioA.send(data, 1);
            REQUIRE(temporary.pending() == 1);
        }
        uint8_t data[] = {'2'};
        REQUIRE(radioA.send(data, 1) == true);   // must not touch the destroyed radio
        REQUIRE(radioB.pending() == 2);
    }

    SECTION("Injected packets go to one radio only") {
        uint8_t data[] = {'i'};
        radioB.injectPacket(data, 1);
        REQUIRE(radioB.pending() == 1);
        REQUIRE(radioA.pending() == 0);
        REQUIRE(radioC.pending() == 0);
    }
}

TEST_CASE("MockRadio buffer handling", "[MockRadio]") {
    MockRadio radio;
    MockRadio other;
    MockRadio::clearChannel();

    SECTION("Handles buffer size limits") {
        uint8_t largeData[300];
        for (int i = 0; i < 300; i++) {
            largeData[i] = i & 0xFF;
        }

        REQUIRE(radio.send(largeData, 300) == true);

        // Try to receive into small buffer
        uint8_t smallBuffer[10];
        int received = other.receive(smallBuffer, 10);
        REQUIRE(received == 0); // Should reject if doesn't fit
    }

    SECTION("Handles exact buffer size") {
        uint8_t data[10];
        for (int i = 0; i < 10; i++) {
            data[i] = i;
        }

        REQUIRE(radio.send(data, 10) == true);

        uint8_t buffer[10];
        int received = other.receive(buffer, 10);
        REQUIRE(received == 10);

        for (int i = 0; i < 10; i++) {
            REQUIRE(buffer[i] == i);
        }
    }

    SECTION("Handles empty data") {
        REQUIRE(radio.send(nullptr, 0) == true);

        uint8_t buffer[10];
        int received = other.receive(buffer, 10);
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
