/**
 * @file main.cpp
 * @brief LoRaPeerLink hardware bench test
 *
 * Flash the same firmware to two or more boards. Each board
 *   - names itself after its chip ID ("bench-A1B2"),
 *   - discovers the others (RollCall),
 *   - sends an acknowledged PING to every known peer every few seconds,
 *   - checks what it receives for gaps and duplicates,
 *   - prints one line per event and a statistics block every 30 seconds.
 *
 * Everything is reported on the serial console (115200 baud). No display is
 * needed. Type 'h' for the list of commands.
 *
 * What a healthy run looks like is described in README.md next to this file
 * and in TEST_PLAN.md (section 4) at the top of the repository.
 */

#include <Arduino.h>
#include <esp_system.h>
#include <map>

#include <SemtechRadio.h>
#include <LoraBackoffLink.h>
#include <EncryptedLoRaLink.h>
#include <RollCall.h>
#include <PeerMessenger.h>

// ---------------------------------------------------------------------------
// Configuration (override with -D on the command line or in platformio.ini)
// ---------------------------------------------------------------------------

#ifndef BENCH_FREQUENCY_HZ
#define BENCH_FREQUENCY_HZ 915000000
#endif
#ifndef BENCH_SPREADING_FACTOR
#define BENCH_SPREADING_FACTOR 7
#endif
#ifndef BENCH_TX_POWER_DBM
#define BENCH_TX_POWER_DBM 14
#endif
#ifndef BENCH_PING_INTERVAL_MS
#define BENCH_PING_INTERVAL_MS 5000
#endif
#ifndef BENCH_STATS_INTERVAL_MS
#define BENCH_STATS_INTERVAL_MS 30000
#endif
#ifndef BENCH_NETWORK
#define BENCH_NETWORK "lpl-bench"
#endif
#ifndef BENCH_PASSWORD
#define BENCH_PASSWORD "not-a-real-secret"
#endif

// ---------------------------------------------------------------------------
// Platform hooks
// ---------------------------------------------------------------------------

static uint32_t get_time_ms() { return millis(); }
static void sleep_ms(uint32_t ms) { delay(ms); }
static uint16_t random_16() { return static_cast<uint16_t>(esp_random()); }
#ifdef BENCH_ENCRYPTED
static void random_bytes(uint8_t* buffer, size_t len) { esp_fill_random(buffer, len); }
#endif

static bool verbose = false;
static void library_log(const char* message) {
    if (verbose) {
        Serial.printf("[%9.3f]      %s\n", millis() / 1000.0, message);
    }
}

// ---------------------------------------------------------------------------
// The stack
// ---------------------------------------------------------------------------

static SemtechRadio radio(BENCH_FREQUENCY_HZ, BENCH_SPREADING_FACTOR, BENCH_TX_POWER_DBM);
static LoRaBackoffLink lora_link(&radio, get_time_ms, sleep_ms);
#ifdef BENCH_ENCRYPTED
static EncryptedLoRaLink* secure_link = nullptr;
#endif
static RollCall* roll_call = nullptr;
static PeerMessenger* messenger = nullptr;
static bool ready = false;

// ---------------------------------------------------------------------------
// Counters
// ---------------------------------------------------------------------------

struct Counters {
    uint32_t pingsSent = 0;
    uint32_t pingsAcked = 0;
    uint32_t pingsFailed = 0;
    uint32_t ackTimeSumMs = 0;
    uint32_t ackTimeMinMs = 0xFFFFFFFF;
    uint32_t ackTimeMaxMs = 0;
    uint32_t pingsReceived = 0;
    uint32_t duplicates = 0;       // Same PING delivered twice: must stay 0
    uint32_t missing = 0;          // PING numbers never seen (the sender's failed sends)
    uint32_t broadcastsSent = 0;
    uint32_t broadcastsReceived = 0;
    uint32_t bigSent = 0;          // Maximum-length messages
    uint32_t bigAcked = 0;
    uint32_t bigReceivedOk = 0;
    uint32_t bigReceivedBad = 0;   // Wrong length or content: must stay 0
    uint32_t otherReceived = 0;
    int minRssi = 0;
    int maxRssi = -200;
};

static Counters counters;
static std::map<uint16_t, uint32_t> last_ping_from;   // Highest PING number seen per sender
static uint32_t ping_number = 0;
static uint32_t broadcast_number = 0;
static uint32_t next_ping_at = 0;
static uint32_t next_stats_at = 0;
static volatile bool button_pressed = false;

static double now_s() { return millis() / 1000.0; }

static String peer_label(uint16_t id) {
    const auto& names = roll_call->getIdToNameMap();
    auto it = names.find(id);
    String label = (it != names.end()) ? String(it->second.c_str()) : String("?");
    label += " (";
    label += String(id);
    label += ")";
    return label;
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

static void print_peers() {
    const auto& names = roll_call->getIdToNameMap();
    Serial.printf("  peers known: %u\n", static_cast<unsigned>(names.size() - 1));
    for (const auto& entry : names) {
        if (entry.first == roll_call->getNodeId()) continue;
        Serial.printf("    %-20s id %u\n", entry.second.c_str(), entry.first);
    }
}

static void print_stats() {
    const LoRaLinkCore::Stats& link = lora_link.stats();
    Serial.println();
    Serial.printf("==== %s (id %u)  up %.0f s  SF%d ====\n", roll_call->getNodeName().c_str(), roll_call->getNodeId(), now_s(), BENCH_SPREADING_FACTOR);
    print_peers();
    const uint32_t finished = counters.pingsAcked + counters.pingsFailed;
    Serial.printf("  pings sent        %lu   acked %lu   failed %lu   (%.1f%% delivered)\n",
                  static_cast<unsigned long>(counters.pingsSent), static_cast<unsigned long>(counters.pingsAcked),
                  static_cast<unsigned long>(counters.pingsFailed),
                  finished ? 100.0 * counters.pingsAcked / finished : 0.0);
    if (counters.pingsAcked > 0) {
        Serial.printf("  time to ACK       min %lu ms   avg %lu ms   max %lu ms\n",
                      static_cast<unsigned long>(counters.ackTimeMinMs),
                      static_cast<unsigned long>(counters.ackTimeSumMs / counters.pingsAcked),
                      static_cast<unsigned long>(counters.ackTimeMaxMs));
    }
    Serial.printf("  pings received    %lu   duplicates %lu (must be 0)   missing %lu\n",
                  static_cast<unsigned long>(counters.pingsReceived), static_cast<unsigned long>(counters.duplicates),
                  static_cast<unsigned long>(counters.missing));
    Serial.printf("  broadcasts        sent %lu   received %lu\n",
                  static_cast<unsigned long>(counters.broadcastsSent), static_cast<unsigned long>(counters.broadcastsReceived));
    Serial.printf("  max-size messages sent %lu   acked %lu   received intact %lu   corrupt %lu (must be 0)\n",
                  static_cast<unsigned long>(counters.bigSent), static_cast<unsigned long>(counters.bigAcked),
                  static_cast<unsigned long>(counters.bigReceivedOk), static_cast<unsigned long>(counters.bigReceivedBad));
    if (counters.maxRssi > -200) {
        Serial.printf("  RSSI              %d to %d dBm\n", counters.minRssi, counters.maxRssi);
    }
    Serial.printf("  link  tx frames %lu  retransmits %lu  acks sent %lu  sends failed %lu\n",
                  static_cast<unsigned long>(link.txFrames), static_cast<unsigned long>(link.txRetransmits),
                  static_cast<unsigned long>(link.txAcks), static_cast<unsigned long>(link.txFailed));
    Serial.printf("  link  rx frames %lu  duplicates suppressed %lu  invalid %lu  foreign %lu  dropped %lu\n",
                  static_cast<unsigned long>(link.rxFrames), static_cast<unsigned long>(link.rxDuplicates),
                  static_cast<unsigned long>(link.rxInvalid), static_cast<unsigned long>(link.rxForeign),
                  static_cast<unsigned long>(link.rxDropped));
    Serial.printf("  radio frames sent %lu  received %lu  crc errors %lu  tx timeouts %lu\n",
                  static_cast<unsigned long>(radio.framesSent()), static_cast<unsigned long>(radio.framesReceived()),
                  static_cast<unsigned long>(radio.crcErrors()), static_cast<unsigned long>(radio.txTimeouts()));
#ifdef BENCH_ENCRYPTED
    Serial.printf("  encryption        rejected packets %lu\n", static_cast<unsigned long>(secure_link->rejectedPackets()));
#endif
    Serial.printf("  free heap         %lu bytes\n", static_cast<unsigned long>(ESP.getFreeHeap()));
    Serial.println();
}

static void print_help() {
    Serial.println();
    Serial.println("Commands:");
    Serial.println("  s  statistics now");
    Serial.println("  n  list known peers");
    Serial.println("  p  ping all peers now");
    Serial.println("  f  flood: 10 pings in a row to every peer (do it on two boards at once to force collisions)");
    Serial.println("  b  send a broadcast (the button on GPIO0 does the same)");
    Serial.println("  m  send a maximum-length message to every peer and check it arrives intact");
    Serial.println("  q  RF check: sample the channel signal level for 6 s");
    Serial.println("  x  RF check: transmit a burst of 30 broadcast frames");
    Serial.println("  v  toggle verbose library log");
    Serial.println("  r  reset counters");
    Serial.println("  h  this help");
    Serial.println();
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

static void ping_peer(uint16_t id) {
    const uint32_t number = ++ping_number;
    const String text = "PING " + String(number);
    ++counters.pingsSent;

    const uint32_t retransmitsBefore = lora_link.stats().txRetransmits;
    const uint32_t start = millis();
    const bool acked = messenger->sendMessage(id, std::string(text.c_str()), true);
    const uint32_t took = millis() - start;
    const uint32_t retries = lora_link.stats().txRetransmits - retransmitsBefore;

    if (acked) {
        ++counters.pingsAcked;
        counters.ackTimeSumMs += took;
        if (took < counters.ackTimeMinMs) counters.ackTimeMinMs = took;
        if (took > counters.ackTimeMaxMs) counters.ackTimeMaxMs = took;
        Serial.printf("[%9.3f] TX   PING %lu -> %s  ACK after %lu ms%s\n", now_s(), static_cast<unsigned long>(number),
                      peer_label(id).c_str(), static_cast<unsigned long>(took),
                      retries ? (String("  (") + String(retries) + " retransmission" + (retries > 1 ? "s)" : ")")).c_str() : "");
    } else {
        ++counters.pingsFailed;
        Serial.printf("[%9.3f] TX   PING %lu -> %s  NO ACK after %lu ms and %lu transmissions\n", now_s(),
                      static_cast<unsigned long>(number), peer_label(id).c_str(), static_cast<unsigned long>(took),
                      static_cast<unsigned long>(retries + 1));
    }
}

static void ping_all_peers(int repeat) {
    // Copy the IDs first: sending also receives, which can change the table.
    uint16_t ids[16];
    size_t count = 0;
    for (const auto& entry : roll_call->getIdToNameMap()) {
        if (entry.first != roll_call->getNodeId() && count < 16) ids[count++] = entry.first;
    }
    if (count == 0) {
        Serial.printf("[%9.3f]      no peers known yet\n", now_s());
        return;
    }
    for (int r = 0; r < repeat; ++r) {
        for (size_t i = 0; i < count; ++i) ping_peer(ids[i]);
    }
}

// The longest message the stack allows, filled with a known pattern, so the
// receiver can check every byte. On a plain link this is a full 255 byte
// LoRa frame.
static std::string big_message(uint32_t number, size_t length) {
    std::string text = "BIG " + std::to_string(number) + " ";
    while (text.size() < length) text.push_back(static_cast<char>('A' + text.size() % 26));
    text.resize(length);
    return text;
}

static void send_big_to_all_peers() {
    uint16_t ids[16];
    size_t count = 0;
    for (const auto& entry : roll_call->getIdToNameMap()) {
        if (entry.first != roll_call->getNodeId() && count < 16) ids[count++] = entry.first;
    }
    if (count == 0) {
        Serial.printf("[%9.3f]      no peers known yet\n", now_s());
        return;
    }
    const size_t length = messenger->maxMessageLength();
    for (size_t i = 0; i < count; ++i) {
        const uint32_t number = ++counters.bigSent;
        const uint32_t start = millis();
        const bool acked = messenger->sendMessage(ids[i], big_message(number, length), true);
        if (acked) ++counters.bigAcked;
        Serial.printf("[%9.3f] TX   BIG %lu (%u bytes) -> %s  %s after %lu ms\n", now_s(), static_cast<unsigned long>(number),
                      static_cast<unsigned>(length), peer_label(ids[i]).c_str(), acked ? "ACK" : "NO ACK",
                      static_cast<unsigned long>(millis() - start));
    }
}

static void send_broadcast() {
    const String text = "BCAST " + String(++broadcast_number) + " from " + String(roll_call->getNodeName().c_str());
    const bool sent = messenger->broadcastMessage(std::string(text.c_str()));
    if (sent) ++counters.broadcastsSent;
    Serial.printf("[%9.3f] TX   %s  %s\n", now_s(), text.c_str(), sent ? "sent" : "RADIO FAILED");
}

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------

static void handle_received(const UserMessage& msg) {
    const int rssi = radio.packetRssi();   // Of the most recent frame; close enough for a bench test
    if (rssi < counters.minRssi) counters.minRssi = rssi;
    if (rssi > counters.maxRssi) counters.maxRssi = rssi;

    const String content(msg.content.c_str());
    if (content.startsWith("PING ")) {
        const uint32_t number = static_cast<uint32_t>(content.substring(5).toInt());
        ++counters.pingsReceived;
        const char* note = "";
        auto it = last_ping_from.find(msg.srcId);
        if (it != last_ping_from.end()) {
            if (number == it->second) {
                ++counters.duplicates;
                note = "  DUPLICATE";
            } else if (number > it->second + 1) {
                counters.missing += number - it->second - 1;
                note = "  (gap: the sender's earlier pings did not arrive)";
            } else if (number < it->second) {
                note = "  (sender restarted)";
            }
        }
        last_ping_from[msg.srcId] = number;
        Serial.printf("[%9.3f] RX   PING %lu <- %s  rssi %d dBm%s\n", now_s(), static_cast<unsigned long>(number),
                      peer_label(msg.srcId).c_str(), rssi, note);
    } else if (content.startsWith("BIG ")) {
        const uint32_t number = static_cast<uint32_t>(content.substring(4).toInt());
        const bool intact = msg.content == big_message(number, messenger->maxMessageLength());
        if (intact) ++counters.bigReceivedOk; else ++counters.bigReceivedBad;
        Serial.printf("[%9.3f] RX   BIG %lu (%u bytes) <- %s  rssi %d dBm  %s\n", now_s(), static_cast<unsigned long>(number),
                      static_cast<unsigned>(msg.content.size()), peer_label(msg.srcId).c_str(), rssi,
                      intact ? "content intact" : "CORRUPT");
    } else if (content.startsWith("BCAST ")) {
        ++counters.broadcastsReceived;
        Serial.printf("[%9.3f] RX   %s  rssi %d dBm\n", now_s(), content.c_str(), rssi);
    } else {
        ++counters.otherReceived;
        Serial.printf("[%9.3f] RX   \"%s\" <- %s  rssi %d dBm\n", now_s(), content.c_str(), peer_label(msg.srcId).c_str(), rssi);
    }
}

// ---------------------------------------------------------------------------
// RF diagnostics (no peers needed)
// ---------------------------------------------------------------------------

// Sample the channel signal level for a few seconds. Run this on one board
// while another board transmits ('x'): if the level does not rise during the
// other board's transmissions, the problem is RF (antenna, power, frequency),
// not the protocol.
static void rssi_scan(uint32_t durationMs) {
    Serial.printf("[%9.3f] RSSI scan for %lu ms (one sample every 20 ms; values in dBm)\n", now_s(), static_cast<unsigned long>(durationMs));
    const uint32_t start = millis();
    int lowest = 0, highest = -200, column = 0;
    long sum = 0, count = 0;
    while (millis() - start < durationMs) {
        const int rssi = radio.channelRssi();
        if (rssi < lowest) lowest = rssi;
        if (rssi > highest) highest = rssi;
        sum += rssi;
        ++count;
        Serial.printf("%5d", rssi);
        if (++column == 25) { Serial.println(); column = 0; }
        delay(20);
    }
    if (column) Serial.println();
    Serial.printf("[%9.3f] RSSI scan done: min %d, avg %ld, max %d dBm, %ld samples; frames received so far %lu, crc errors %lu\n",
                  now_s(), lowest, count ? sum / count : 0, highest, count,
                  static_cast<unsigned long>(radio.framesReceived()), static_cast<unsigned long>(radio.crcErrors()));
}

// Transmit a burst of raw broadcast frames straight through the link layer.
static void tx_burst(int frames) {
    Serial.printf("[%9.3f] TX burst: %d broadcast frames\n", now_s(), frames);
    int ok = 0;
    for (int i = 0; i < frames; ++i) {
        char text[40];
        const int n = snprintf(text, sizeof(text), "BURST %d of %d ........", i + 1, frames);
        if (lora_link.sendPacket(roll_call->getNodeId(), 0xFFFF, reinterpret_cast<const uint8_t*>(text), static_cast<uint8_t>(n))) ++ok;
        delay(60);
    }
    Serial.printf("[%9.3f] TX burst done: %d of %d transmitted (radio tx timeouts %lu)\n", now_s(), ok, frames,
                  static_cast<unsigned long>(radio.txTimeouts()));
}

static void IRAM_ATTR on_button() {
    static volatile unsigned long last = 0;
    const unsigned long now = millis();
    if (now - last > 250) button_pressed = true;
    last = now;
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    // With native USB the port only exists once the host opens it; wait a
    // little so the banner is not lost, but do not wait forever.
    const uint32_t waitStart = millis();
    while (!Serial && millis() - waitStart < 3000) delay(10);
    delay(200);

    const uint64_t mac = ESP.getEfuseMac();
    char name[24];
#ifdef BENCH_SAME_NAME
    // Every board asks for the same name, to exercise RollCall's name
    // collision handling: exactly one board should keep it.
    (void)mac;
    snprintf(name, sizeof(name), "twin");
#else
    snprintf(name, sizeof(name), "bench-%04X", static_cast<unsigned>((mac >> 32) & 0xFFFF));
#endif

    Serial.println();
    Serial.println("========================================");
    Serial.println(" LoRaPeerLink bench test");
#ifdef BENCH_ENCRYPTED
    Serial.println(" link: LoRaBackoffLink + EncryptedLoRaLink");
#else
    Serial.println(" link: LoRaBackoffLink (not encrypted)");
#endif
    Serial.printf(" node: %s\n", name);
    Serial.printf(" radio: %.3f MHz, SF%d, 125 kHz, %d dBm\n", BENCH_FREQUENCY_HZ / 1e6, BENCH_SPREADING_FACTOR, BENCH_TX_POWER_DBM);
    Serial.println("========================================");

    pinMode(0, INPUT_PULLUP);
    attachInterrupt(0, on_button, FALLING);

    if (!radio.begin()) {
        Serial.println("ERROR: radio.begin() failed");
        return;
    }
    // A direct check of the radio settings: the simulator in the test suite
    // expects 45 ms for a 9 byte frame at SF7 / 125 kHz / 12 symbol preamble.
    Serial.printf(" time on air: 9 byte ACK %lu ms, 25 byte frame %lu ms, 255 byte frame %lu ms\n",
                  static_cast<unsigned long>(radio.timeOnAirMs(9)), static_cast<unsigned long>(radio.timeOnAirMs(25)),
                  static_cast<unsigned long>(radio.timeOnAirMs(255)));
    Serial.printf(" ACK timeout configured: %lu ms\n", static_cast<unsigned long>(lora_link.ackTimeoutMs()));

    ILoRaLink* top = &lora_link;
#ifdef BENCH_ENCRYPTED
    const uint32_t kdfStart = millis();
    secure_link = new EncryptedLoRaLink(&lora_link, BENCH_NETWORK, BENCH_PASSWORD,
                                        EncryptedLoRaLink::DEFAULT_KEY_ITERATIONS, get_time_ms, random_bytes);
    Serial.printf(" key derivation took %lu ms\n", static_cast<unsigned long>(millis() - kdfStart));
    top = secure_link;
#endif

    roll_call = new RollCall(top, name, get_time_ms, sleep_ms, random_16, library_log);
    messenger = new PeerMessenger(roll_call, library_log);

    Serial.println(" joining (about 1.5 s)...");
    if (!roll_call->begin()) {
        Serial.println("ERROR: RollCall::begin() failed (radio could not transmit?)");
        Serial.printf(" radio: frames sent %lu, tx timeouts %lu\n", static_cast<unsigned long>(radio.framesSent()),
                      static_cast<unsigned long>(radio.txTimeouts()));
        return;
    }
    messenger->begin();

    Serial.printf(" joined as %s, id %u\n", roll_call->getNodeName().c_str(), roll_call->getNodeId());
    Serial.printf(" largest message: %u bytes\n", static_cast<unsigned>(messenger->maxMessageLength()));
    print_help();

    // Spread the boards out a little so they do not all ping at once.
    next_ping_at = millis() + 3000 + esp_random() % 3000;
    next_stats_at = millis() + BENCH_STATS_INTERVAL_MS;
    ready = true;
}

void loop() {
    if (!ready) {
        delay(1000);
        return;
    }

    // Listen. Short enough that commands feel responsive; the radio keeps
    // receiving between calls.
    messenger->processMessages(250);
    while (messenger->hasMessage()) {
        handle_received(messenger->receiveMessage());
    }

    if (button_pressed) {
        button_pressed = false;
        send_broadcast();
    }

    while (Serial.available() > 0) {
        const int c = Serial.read();
        switch (c) {
            case 's': print_stats(); break;
            case 'n': print_peers(); break;
            case 'p': ping_all_peers(1); break;
            case 'f': ping_all_peers(10); break;
            case 'b': send_broadcast(); break;
            case 'm': send_big_to_all_peers(); break;
            case 'q': rssi_scan(6000); break;
            case 'x': tx_burst(30); break;
            case 'v':
                verbose = !verbose;
                Serial.printf("verbose %s\n", verbose ? "on" : "off");
                break;
            case 'r':
                counters = Counters();
                last_ping_from.clear();
                Serial.println("counters reset");
                break;
            case 'h':
            case '?': print_help(); break;
            default: break;   // Ignore line endings and anything else
        }
    }

    const uint32_t now = millis();
    if (static_cast<int32_t>(now - next_ping_at) >= 0) {
        ping_all_peers(1);
        next_ping_at = millis() + BENCH_PING_INTERVAL_MS + esp_random() % 1000;
    }
    if (static_cast<int32_t>(now - next_stats_at) >= 0) {
        print_stats();
        next_stats_at = millis() + BENCH_STATS_INTERVAL_MS;
    }
}
