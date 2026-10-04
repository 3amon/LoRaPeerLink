/**
 * @file encryption_cli_tool.cpp
 * @brief Command line access to the library's cryptography, for cross-validation
 *
 * validate_encryption.py drives this tool and compares every result with an
 * independent implementation (Python's hashlib and "cryptography" package).
 *
 * Usage (all binary values are hex strings; use "-" for an empty value):
 *   encryption_cli_tool keys   <network> <password> <iterations>
 *   encryption_cli_tool seal   <network> <password> <iterations> <srcId> <iv> <plaintext>
 *   encryption_cli_tool open   <network> <password> <iterations> <srcId> <wire>
 *   encryption_cli_tool sha256 <data>
 *   encryption_cli_tool hmac   <key> <data>
 *   encryption_cli_tool aes    <key> <block>
 *   encryption_cli_tool pbkdf2 <password> <salt> <iterations> <length>
 *
 * "open" prints "OK <plaintext>" or "REJECTED". Exit status is 0 unless the
 * arguments are malformed.
 */

#include "EncryptedLoRaLink.h"
#include "LplCrypto.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

class NullLink : public ILoRaLink {
public:
    bool sendPacket(uint16_t, uint16_t, const uint8_t*, uint8_t, bool, int) override { return true; }
    int receivePacket(uint16_t*, uint8_t*, uint8_t, uint32_t) override { return 0; }
    void setLocalId(uint16_t) override {}
};

bool unhex(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    if (text == "-") return true;
    if (text.size() % 2 != 0) return false;
    for (size_t i = 0; i < text.size(); i += 2) {
        char* end = nullptr;
        const std::string byte = text.substr(i, 2);
        const long v = strtol(byte.c_str(), &end, 16);
        if (end != byte.c_str() + 2) return false;
        out.push_back(static_cast<uint8_t>(v));
    }
    return true;
}

void printHex(const uint8_t* data, size_t len) {
    if (len == 0) printf("-");
    for (size_t i = 0; i < len; ++i) printf("%02x", data[i]);
    printf("\n");
}

int usage() {
    fprintf(stderr, "usage: encryption_cli_tool keys|seal|open|sha256|hmac|aes|pbkdf2 ... (see the source file)\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    std::vector<uint8_t> a, b;

    if (cmd == "sha256" && argc == 3) {
        if (!unhex(argv[2], a)) return usage();
        uint8_t digest[32];
        lpl::sha256(a.data(), a.size(), digest);
        printHex(digest, 32);
        return 0;
    }
    if (cmd == "hmac" && argc == 4) {
        if (!unhex(argv[2], a) || !unhex(argv[3], b)) return usage();
        uint8_t mac[32];
        lpl::hmacSha256(a.data(), a.size(), b.data(), b.size(), mac);
        printHex(mac, 32);
        return 0;
    }
    if (cmd == "aes" && argc == 4) {
        if (!unhex(argv[2], a) || !unhex(argv[3], b) || a.size() != 16 || b.size() != 16) return usage();
        lpl::Aes128 aes(a.data());
        uint8_t out[16];
        aes.encryptBlock(b.data(), out);
        printHex(out, 16);
        return 0;
    }
    if (cmd == "pbkdf2" && argc == 6) {
        if (!unhex(argv[2], a) || !unhex(argv[3], b)) return usage();
        const size_t len = static_cast<size_t>(atoi(argv[5]));
        if (len == 0 || len > 256) return usage();
        std::vector<uint8_t> out(len);
        lpl::pbkdf2HmacSha256(a.data(), a.size(), b.data(), b.size(), static_cast<uint32_t>(strtoul(argv[4], nullptr, 10)), out.data(), len);
        printHex(out.data(), len);
        return 0;
    }

    if ((cmd == "keys" && argc == 5) || (cmd == "seal" && argc == 8) || (cmd == "open" && argc == 7)) {
        NullLink null;
        EncryptedLoRaLink link(&null, argv[2], argv[3], static_cast<uint32_t>(strtoul(argv[4], nullptr, 10)));
        if (cmd == "keys") {
            uint8_t keys[32];
            link.exportKeysForTesting(keys);
            printHex(keys, 32);
            return 0;
        }
        const uint16_t srcId = static_cast<uint16_t>(strtoul(argv[5], nullptr, 10));
        if (cmd == "seal") {
            if (!unhex(argv[6], a) || !unhex(argv[7], b) || a.size() != 16) return usage();
            uint8_t wire[512];
            const size_t n = link.seal(srcId, a.data(), b.data(), b.size(), wire, sizeof(wire));
            if (n == 0) {
                printf("FAILED\n");
            } else {
                printHex(wire, n);
            }
            return 0;
        }
        if (!unhex(argv[6], a)) return usage();
        uint8_t plain[512];
        size_t plainLen = 0;
        if (link.open(srcId, a.data(), a.size(), plain, sizeof(plain), plainLen)) {
            printf("OK ");
            printHex(plain, plainLen);
        } else {
            printf("REJECTED\n");
        }
        return 0;
    }

    return usage();
}
