/**
 * @file LoRaLinkCore.h
 * @brief Shared implementation of the LoRaPeerLink link layer
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * LoRaBasicLink and LoRaBackoffLink are both thin wrappers around this class.
 * They speak exactly the same frame format and differ only in how long they
 * wait before a transmission and between retries.
 *
 * Frame format (all multi-byte fields big-endian):
 *
 *   [DestID:16][SrcID:16][Seq:8][Flags:8][PayloadLen:8][Payload...][CRC16:16]
 *
 * The CRC is CRC-16/CCITT-FALSE over everything before it. A whole frame is at
 * most 255 bytes, the largest LoRa PHY payload.
 *
 * Behaviour that matters on a real half-duplex radio:
 *
 *   - Waiting for an ACK is done with long receive windows. A radio driver
 *     that only listens inside receive() could never hear an ACK with the
 *     short polling windows used by version 1.
 *   - A frame for this node that arrives while it is waiting for an ACK (or
 *     backing off) is acknowledged and kept in a small queue; the next call
 *     to receivePacket() returns it. Version 1 threw such frames away.
 *   - Retransmissions reuse the sequence number and the receiver suppresses
 *     duplicates, so a lost ACK does not deliver the payload twice.
 *   - An ACK is only accepted if it has a valid CRC, comes from the node the
 *     frame was sent to, is addressed to the sender and carries the right
 *     sequence number.
 *   - receivePacket() keeps listening for its whole timeout; hearing a frame
 *     for somebody else does not end the call early.
 */

#ifndef LORA_LINK_CORE_H
#define LORA_LINK_CORE_H

#include "ILoRaLink.h"
#include "IRadio.h"

#include <stddef.h>
#include <stdint.h>

#ifndef BROADCAST_ADDR
#define BROADCAST_ADDR 0xFFFF      ///< Address for broadcast messages (16-bit)
#endif
#ifndef BUFFER_SIZE
#define BUFFER_SIZE    256         ///< Size of scratch buffers used for radio I/O
#endif

#define LORA_MAX_FRAME 255         ///< Largest frame a LoRa radio can carry
#define HEADER_SIZE    7           ///< dst(2) + src(2) + seq + flags + len
#define CRC_SIZE       2           ///< CRC-16 trailer
#define MAX_PAYLOAD    (LORA_MAX_FRAME - HEADER_SIZE - CRC_SIZE)  ///< Largest payload: 246 bytes

/**
 * Number of received payloads the link can hold while it is busy sending.
 * Each slot costs MAX_PAYLOAD + 4 bytes of RAM.
 */
#ifndef LORA_LINK_RX_QUEUE_SIZE
#define LORA_LINK_RX_QUEUE_SIZE 4
#endif

/** Number of recently acknowledged frames remembered for duplicate suppression. */
#ifndef LORA_LINK_DUP_TABLE_SIZE
#define LORA_LINK_DUP_TABLE_SIZE 8
#endif

class LoRaLinkCore : public ILoRaLink {
public:
    static constexpr uint8_t FLAG_ACK = 0x01;         ///< Frame is an acknowledgment
    static constexpr uint8_t FLAG_ACK_REQUEST = 0x02; ///< Sender wants an acknowledgment

    /** Counters for diagnostics. All start at zero. */
    struct Stats {
        uint32_t txFrames = 0;        ///< Data frames put on the air (including retransmissions)
        uint32_t txRetransmits = 0;   ///< Retransmissions after a missing ACK
        uint32_t txAcks = 0;          ///< ACK frames sent
        uint32_t txFailed = 0;        ///< sendPacket() calls that gave up without an ACK
        uint32_t rxFrames = 0;        ///< Data frames accepted for this node
        uint32_t rxAcks = 0;          ///< Matching ACKs received
        uint32_t rxDuplicates = 0;    ///< Retransmissions recognised and suppressed
        uint32_t rxInvalid = 0;       ///< Frames dropped: too short, bad length or bad CRC
        uint32_t rxForeign = 0;       ///< Valid frames addressed to another node
        uint32_t rxDropped = 0;       ///< Frames for this node dropped (queue full / buffer too small)
    };

    bool sendPacket(uint16_t srcId, uint16_t destId, const uint8_t* payload, uint8_t len,
                    bool requestAck = false, int maxRetries = 3) override;

    int receivePacket(uint16_t* srcId, uint8_t* buffer, uint8_t maxLen, uint32_t timeoutMs = 1000) override;

    void setLocalId(uint16_t localId) override;

    uint8_t maxPayloadSize() const override { return MAX_PAYLOAD; }

    /**
     * @brief Set how long to wait for an ACK after each transmission.
     *
     * The wait has to cover the time the receiver needs to turn around plus
     * the time on air of a 9 byte ACK. That is about 50 ms at SF7/125 kHz but
     * more than a second at SF12. If the radio driver implements
     * IRadio::timeOnAirMs() the link raises the wait automatically when the
     * configured value is too short for the modem settings.
     */
    void setAckTimeoutMs(uint32_t ms) { _ackTimeoutMs = ms; }
    uint32_t ackTimeoutMs() const { return _ackTimeoutMs; }

    /**
     * How long a received frame is remembered for duplicate suppression.
     * The link uses a longer window automatically when the modem is slow
     * enough that retransmissions could be further apart than this.
     */
    void setDuplicateWindowMs(uint32_t ms) { _dupWindowMs = ms; }
    uint32_t duplicateWindowMs() const { return _dupWindowMs; }

    const Stats& stats() const { return _stats; }

    /** Number of received payloads waiting to be returned by receivePacket(). */
    uint8_t pendingCount() const { return _queueCount; }

    /** CRC-16/CCITT-FALSE (polynomial 0x1021, initial value 0xFFFF). */
    static uint16_t crc16(const uint8_t* data, size_t len);

protected:
    LoRaLinkCore(IRadio* radio, time_ms_fn getTime, sleep_ms_fn sleep, uint32_t ackTimeoutMs);

    /**
     * Delay before transmission attempt @p attempt (0 = first). The link
     * listens during the delay. @p slotMs is the backoff slot for this frame
     * (see slotMs()).
     */
    virtual uint32_t preSendDelayMs(int attempt, uint32_t slotMs) { (void)attempt; (void)slotMs; return 0; }

    /**
     * Delay after attempt @p attempt went unacknowledged, before the next one.
     * The default is truncated binary exponential backoff: a random delay
     * from a window that starts at two slots and doubles with every failed
     * attempt, up to MAX_BACKOFF_SLOTS slots.
     */
    virtual uint32_t retryDelayMs(int attempt, uint32_t slotMs);

    /**
     * Backoff slot for a frame of @p frameLen bytes: the time one exchange
     * (frame plus ACK) occupies the channel. Uses IRadio::timeOnAirMs() when
     * the driver provides it and DEFAULT_SLOT_MS otherwise.
     */
    uint32_t slotMs(size_t frameLen);

    static constexpr uint32_t DEFAULT_SLOT_MS = 100;    ///< Slot when the radio cannot report time on air
    static constexpr uint32_t MAX_BACKOFF_SLOTS = 16;   ///< Largest backoff window, in slots

    /** Uniform pseudo-random number in [0, bound). Not for cryptographic use. */
    uint32_t randomBelow(uint32_t bound);

private:
    struct Frame {
        uint16_t dst;
        uint16_t src;
        uint8_t seq;
        uint8_t flags;
        uint8_t len;
        const uint8_t* payload;
        uint16_t crc;
    };

    struct Pending {
        uint16_t src;
        uint8_t len;
        uint8_t data[MAX_PAYLOAD];
    };

    struct Seen {
        uint16_t src;
        uint16_t crc;
        uint32_t timeMs;
        uint8_t seq;
        bool valid;
    };

    static bool decode(const uint8_t* raw, int rawLen, Frame& out);
    static size_t encode(uint8_t* out, uint16_t dst, uint16_t src, uint8_t seq, uint8_t flags,
                         const uint8_t* payload, uint8_t len);

    bool isForUs(const Frame& f) const { return f.dst == _localId || f.dst == BROADCAST_ADDR; }
    /** @return payload length if the frame was delivered to @p out, -1 if it was rejected. */
    int acceptData(const Frame& f, uint8_t* out, uint8_t maxLen);
    void queueData(const Frame& f);
    int popPending(uint16_t* srcId, uint8_t* buffer, uint8_t maxLen);
    bool isDuplicate(const Frame& f, size_t frameLen);
    uint32_t effectiveDupWindowMs(size_t frameLen);
    void remember(const Frame& f);
    void sendAck(const Frame& f);
    bool waitForAck(uint16_t srcId, uint16_t destId, uint8_t seq);
    void listenFor(uint32_t ms);
    uint32_t effectiveAckTimeoutMs();

    IRadio* _radio;
    uint16_t _localId;
    uint8_t _seqNum;
    time_ms_fn _getTime;
    sleep_ms_fn _sleep;
    uint32_t _ackTimeoutMs;
    uint32_t _dupWindowMs;
    uint32_t _rngState;
    Stats _stats;

    Pending _queue[LORA_LINK_RX_QUEUE_SIZE];
    uint8_t _queueHead;
    uint8_t _queueCount;

    Seen _seen[LORA_LINK_DUP_TABLE_SIZE];
    uint8_t _seenNext;
};

#endif // LORA_LINK_CORE_H
