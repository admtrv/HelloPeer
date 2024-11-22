/*
 * TCU (Transmission Control over UDP) Protocol Specification
 *
 * Author: Anton Dmitriev
 *
 * Overview:
 * The TCU (Transmission Control over UDP) protocol is a custom protocol,
 * designed to provide reliable data transmission over UDP.
 *
 * Structure:
 * The TCU protocol is defined by simple header followed by the payload data.
 * The protocol operates over UDP, and each UDP datagram carries one TCU packet.
 *
 * Header Fields:
 *
 *                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |               Sequence Number                 |     Flags     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |             Length            |           Checksum            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * 1. Sequence Number:
 *    - Sequence number of the packet
 *    - Ensures that receiver can reassemble data, even if fragments arrive out of order
 *    - When message is fragmented, each fragment has its own sequence number
 *
 * 2. Flags:
 *    - Control flags that are used to indicate the packet's state:
 *        1) SYN (Synchronize) - Initiates a connection or new session
 *        2) ACK (Acknowledgment) - Acknowledges the receipt of a packet
 *        3) FIN (Finish) - Indicates the termination of a session or connection
 *        4) NACK (Negative Acknowledgment) - Requests the retransmission of specific fragment
 *        5) DF (Don't Fragment) - Packet is not fragmented
 *        6) MF (More Fragments) - Packet is part of fragmented message and more fragments expected
 *        7) FL (File Message) - Packet is file message
 *        8) KA (Keep-Alive Message) - Packet is heart-beat message
 *
 * 3. Length:
 *    - Length of the payload in bytes
 *    - This value includes only the payload size, not the header size
 *
 * 4. Checksum:
 *    - Checksum used to verify the integrity of the packet, including the header and payload
 *    - This is calculated using the CRC16-CCITT algorithm over both the header and the payload
 *
 * Selective Repeat (SR) Support:
 *    - The TCU protocol employs Selective Repeat (SR) with Dynamic Window ARQ to ensure reliable data transmission
 *    - SR allows retransmission of only corrupted or lost fragments based on the NACK packets
 *
 * Flags Combinations :
 * 1. Connection Request — SYN, LEN 0
 * 2. Connection Acknowledgment — SYN + ACK, LEN 0
 *
 * 3. Disconnection Request — FIN, LEN 0
 * 4. Disconnection Acknowledgment — FIN + ACK, LEN 0
 *
 * 5. Keep-Alive Request — KA, LEN 0
 * 6. Keep-Alive Acknowledgment — KA + ACK, LEN 0
 *
 * 7. Single Message — DF, LEN
 * 8. Fragment of Message — MF, LEN
 * 9. Last Window Fragment of Message — MF + FIN, LEN
 * 10. Last Fragment of Message — NONE, LEN
 *
 * 11. Single File - DF + FL, LEN
 * 12. Fragment of File — MF + FL, LEN
 * 13. Last Window Fragment of File — MF + FIN + FL, LEN
 * 14. Last Fragment of File — FL, LEN
 *
 * 15. Acknowledgment - ACK, LEN 0, SEQ NUM
 * 16. Negative Acknowledgment — NACK, LEN 0, SEQ NUM [ERR FRG]
 */

#pragma once

#include <cstdint>
#include <vector>
#include <iostream>
#include <cstring>
#include <netinet/in.h>
#include <chrono>
#include <spdlog/spdlog.h>
#include <map>

#include "../types/uint24_t.h"

#define TCU_PHASE_DEAD          0
#define TCU_PHASE_HOLDOFF       1
#define TCU_PHASE_INITIALIZE    2
#define TCU_PHASE_CONNECT       3
#define TCU_PHASE_NETWORK       4
#define TCU_PHASE_DISCONNECT    5
#define TCU_PHASE_CLOSED        6

#define TCU_HDR_NO_FLAG         0x00
#define TCU_HDR_FLAG_SYN        0x01
#define TCU_HDR_FLAG_ACK        0x02
#define TCU_HDR_FLAG_FIN        0x04
#define TCU_HDR_FLAG_NACK       0x08
#define TCU_HDR_FLAG_DF         0x10
#define TCU_HDR_FLAG_MF         0x20
#define TCU_HDR_FLAG_FL         0x40
#define TCU_HDR_FLAG_KA         0x80

#define ETH2_MAX_PAYLOAD_LEN    1500
#define IPV4_HDR_LEN            20
#define UDP_HDR_LEN             8
#define TCU_HDR_LEN             8
#define TCU_MAX_PAYLOAD_LEN     (ETH2_MAX_PAYLOAD_LEN - IPV4_HDR_LEN - UDP_HDR_LEN - TCU_HDR_LEN)

#define TCU_ACTIVITY_TIMEOUT_INTERVAL   300     // 5 minutes (300 seconds) without activities
#define TCU_ACTIVITY_ATTEMPT_COUNT      3       // Number of attempts
#define TCU_ACTIVITY_ATTEMPT_INTERVAL   5       // 5 second interval between attempts

#define TCU_CONNECTION_TIMEOUT_INTERVAL 5       // 5 seconds to get conn ack back
#define TCU_RECEIVE_TIMEOUT_INTERVAL    60      // 1 minute (60 seconds) to get window ack

struct tcu_header {
    uint24_t seq_number;        // Sequence packet number
    uint8_t flags;              // Flags
    uint16_t length;            // Payload length
    uint16_t checksum;          // CRC sum
};

struct tcu_packet {
    tcu_header header{};
    unsigned char* payload;

    tcu_packet();
    ~tcu_packet();

    tcu_packet(const tcu_packet& other);
    tcu_packet& operator=(const tcu_packet& other);

    tcu_packet(tcu_packet&& other) noexcept;
    tcu_packet& operator=(tcu_packet&& other) noexcept;

    unsigned char* to_buff();
    static tcu_packet from_buff(unsigned char* buff);

    void calculate_crc();
    bool validate_crc() ;
};

uint16_t calculate_crc16(const unsigned char* data, size_t length);   // CRC16-CCITT algorithm

/* TCU PCB (Protocol Control Block) */
struct tcu_pcb {
    /* Connection params */
    void new_phase(int phase);
    uint8_t phase = TCU_PHASE_DEAD;     // Phase, where link at

    /* Source node params */
    uint16_t src_port;

    /* Destination node params */
    uint16_t dest_port;
    in_addr dest_ip;
    struct sockaddr_in dest_addr;

    /* Activity params */
    std::atomic<std::chrono::steady_clock::time_point> last_activity;
    std::atomic<bool> is_active{false};
    mutable std::mutex activity_mutex;
    void update_last_activity();
    bool is_activity_recent() const;

};