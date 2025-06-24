/**
 * @file encryption_cli_tool.cpp
 * @brief CLI tool for testing encryption compatibility between Python and C++
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This tool provides a command-line interface to the EncryptedLoRaLink encryption
 * functionality for cross-language validation testing.
 */

#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include "EncryptedLoRaLink.h"
#include "LoraBasicLink.h"
#include "TestUtils.h"

// Helper function to convert hex string to bytes
std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    if (hex.length() % 2 != 0) {
        std::cerr << "Error: Hex string must have even length" << std::endl;
        return bytes;
    }
    
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

// Helper function to convert bytes to hex string
std::string bytesToHex(const uint8_t* data, size_t length) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < length; ++i) {
        ss << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return ss.str();
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <command> [options]\n"
              << "\nCommands:\n"
              << "  encrypt <network_name> <password> <hex_data>\n"
              << "    - Encrypts the given hex data and outputs encrypted hex\n"
              << "  decrypt <network_name> <password> <hex_encrypted_data>\n"
              << "    - Decrypts the given hex data and outputs decrypted hex\n"
              << "  test <network_name> <password> <hex_data>\n"
              << "    - Round-trip test: encrypt then decrypt, verify integrity\n"
              << "\nExamples:\n"
              << "  " << programName << " encrypt TestNetwork Password123 48656c6c6f\n"
              << "  " << programName << " decrypt TestNetwork Password123 <encrypted_hex>\n"
              << "  " << programName << " test TestNetwork Password123 48656c6c6f\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    
    if (command == "encrypt" && argc == 5) {
        std::string networkName = argv[2];
        std::string password = argv[3];
        std::string hexData = argv[4];
        
        // Convert hex input to bytes
        std::vector<uint8_t> inputData = hexToBytes(hexData);
        if (inputData.empty() && !hexData.empty()) {
            std::cerr << "Error: Invalid hex input data" << std::endl;
            return 1;
        }
        
        // Create mock radio and basic link for encryption
        MockRadio mockRadio;
        LoRaBasicLink basicLink(&mockRadio, getTimeMock, sleepMock);
        EncryptedLoRaLink encryptedLink(&basicLink, networkName, password);
        
        // Set up IDs
        encryptedLink.setLocalId(1);
        
        // Encrypt by sending a packet and capturing the raw encrypted data
        bool sent = encryptedLink.sendPacket(1, 2, inputData.data(), inputData.size());
        if (!sent) {
            std::cerr << "Error: Failed to encrypt data" << std::endl;
            return 1;
        }
        
        // Get the raw encrypted packet from the mock radio
        uint8_t rawPacket[256];
        int rawLen = mockRadio.receive(rawPacket, sizeof(rawPacket));
        if (rawLen <= 0) {
            std::cerr << "Error: No encrypted data received" << std::endl;
            return 1;
        }
        
        // Extract the encrypted payload (skip headers: dst(2) + src(2) + seq(1) + flags(1) + len(1) = 7 bytes, then CRC at end)
        // Payload starts at offset 7 and goes until 2 bytes before the end (CRC)
        if (rawLen < 9) { // At least header + 2 bytes payload + CRC
            std::cerr << "Error: Encrypted packet too short" << std::endl;
            return 1;
        }
        
        int payloadStart = 7;
        int payloadLen = rawLen - payloadStart - 2; // Subtract CRC size
        
        if (payloadLen <= 0) {
            std::cerr << "Error: No payload in encrypted packet" << std::endl;
            return 1;
        }
        
        // Output the encrypted payload as hex
        std::cout << bytesToHex(rawPacket + payloadStart, payloadLen) << std::endl;
        
    } else if (command == "decrypt" && argc == 5) {
        std::string networkName = argv[2];
        std::string password = argv[3];
        std::string hexEncrypted = argv[4];
        
        // Convert hex input to bytes
        std::vector<uint8_t> encryptedData = hexToBytes(hexEncrypted);
        if (encryptedData.empty()) {
            std::cerr << "Error: Invalid hex encrypted data" << std::endl;
            return 1;
        }
        
        // Create mock radios and basic links for decryption
        MockRadio radioA, radioB;
        MockRadio::clearChannel();
        
        LoRaBasicLink basicLinkA(&radioA, getTimeMock, sleepMock);
        LoRaBasicLink basicLinkB(&radioB, getTimeMock, sleepMock);
        
        EncryptedLoRaLink encryptedLinkA(&basicLinkA, networkName, password);
        EncryptedLoRaLink encryptedLinkB(&basicLinkB, networkName, password);
        
        encryptedLinkA.setLocalId(1);
        encryptedLinkB.setLocalId(2);
        
        // Create a proper packet structure and inject it
        // Format: [dst(2)][src(2)][seq(1)][flags(1)][len(1)][encrypted_payload][crc(2)]
        std::vector<uint8_t> packet;
        
        // Header: dst=2, src=1, seq=1, flags=0, len=encryptedData.size()
        packet.push_back(0x00); packet.push_back(0x02); // dst = 2
        packet.push_back(0x00); packet.push_back(0x01); // src = 1  
        packet.push_back(0x01); // seq = 1
        packet.push_back(0x00); // flags = 0
        packet.push_back(static_cast<uint8_t>(encryptedData.size())); // payload length
        
        // Add encrypted payload
        packet.insert(packet.end(), encryptedData.begin(), encryptedData.end());
        
        // Add dummy CRC (2 bytes)
        packet.push_back(0x00);
        packet.push_back(0x00);
        
        // Inject packet into radio B's receive buffer
        radioB.injectPacket(packet.data(), packet.size());
        
        // Try to receive and decrypt
        uint16_t srcId;
        uint8_t buffer[200];
        int receivedLen = encryptedLinkB.receivePacket(&srcId, buffer, 200);
        
        if (receivedLen <= 0) {
            std::cerr << "Error: Failed to decrypt data" << std::endl;
            return 1;
        }
        
        // Output the decrypted data as hex
        std::cout << bytesToHex(buffer, receivedLen) << std::endl;
        
    } else if (command == "test" && argc == 5) {
        std::string networkName = argv[2];
        std::string password = argv[3];
        std::string hexData = argv[4];
        
        // Convert hex input to bytes
        std::vector<uint8_t> inputData = hexToBytes(hexData);
        if (inputData.empty() && !hexData.empty()) {
            std::cerr << "Error: Invalid hex input data" << std::endl;
            return 1;
        }
        
        // Create mock radios for round-trip test
        MockRadio radioA, radioB;
        MockRadio::clearChannel();
        
        LoRaBasicLink basicLinkA(&radioA, getTimeMock, sleepMock);  
        LoRaBasicLink basicLinkB(&radioB, getTimeMock, sleepMock);
        
        EncryptedLoRaLink encryptedLinkA(&basicLinkA, networkName, password);
        EncryptedLoRaLink encryptedLinkB(&basicLinkB, networkName, password);
        
        encryptedLinkA.setLocalId(1);
        encryptedLinkB.setLocalId(2);
        
        // Encrypt by sending from A
        bool sent = encryptedLinkA.sendPacket(1, 2, inputData.data(), inputData.size());
        if (!sent) {
            std::cerr << "Error: Failed to send encrypted packet" << std::endl;
            return 1;
        }
        
        // Decrypt by receiving on B
        uint16_t srcId;
        uint8_t buffer[200];
        int receivedLen = encryptedLinkB.receivePacket(&srcId, buffer, 200);
        
        if (receivedLen != static_cast<int>(inputData.size())) {
            std::cerr << "Error: Round-trip test failed - length mismatch" << std::endl;
            std::cerr << "Expected length: " << inputData.size() << ", got: " << receivedLen << std::endl;
            return 1;
        }
        
        if (memcmp(buffer, inputData.data(), inputData.size()) != 0) {
            std::cerr << "Error: Round-trip test failed - data mismatch" << std::endl;
            std::cerr << "Expected: " << bytesToHex(inputData.data(), inputData.size()) << std::endl;
            std::cerr << "Got:      " << bytesToHex(buffer, receivedLen) << std::endl;
            return 1;
        }
        
        std::cout << "SUCCESS: Round-trip test passed" << std::endl;
        std::cout << "Data: " << bytesToHex(buffer, receivedLen) << std::endl;
        
    } else {
        std::cerr << "Error: Invalid command or arguments" << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    
    return 0;
}