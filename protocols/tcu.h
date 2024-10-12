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
 * |            Length             |            Flags              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |       Sequence Number         |           Checksum            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * 1. Length:
 *    - Length of the payload in bytes
 *    - Since the UDP payload is limited to 65507 bytes, 16-bit field is sufficient
 *    - This value includes only the payload size, not the header size
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
 * 3. Sequence Number:
 *    - Sequence number of the packet
 *    - Ensures that receiver can reassemble data, even if fragments arrive out of order
 *    - When message is fragmented, each fragment has its own sequence number
 *
 * 4. Checksum:
 *    - Checksum used to verify the integrity of the packet, including the header and payload
 *    - This is calculated using the CRC16-CCITT algorithm over both the header and the payload

 * Data Flow:
 *    - For connection initiation, a SYN packet is sent
 *    - The receiver responds with an ACK to confirm receipt of the SYN
 *    - For fragmented messages, multiple packets are sent, with the DF and MF flags controlling
 *    - If an error is detected (CRC mismatch), the receiver sends a NACK with the sequence number of the corrupted fragment
 *
 * Data Flow:
 *    - Connection is initiated with a SYN packet from the sender
 *    - Receiver responds with SYN + ACK to confirm the connection establishment
 *    - Once the connection is established, data transmission begins
 *    - For large messages, the sender splits data into fragments.
 *      Packets are marked with DF or MF flags depending on whether fragmentation is used
 *    - Receiver verifies each packet using CRC.
 *      If a packet is corrupted or missing, a NACK is sent with the SEQ NUM of the problematic fragment
 *    - The sender retransmits only the fragments requested in the NACK
 *    - When packets are not transmitted, KA packet is sent to keep connection alive
 *    - If a particular node is desired, the connection is terminated with a FIN packet
 *    - The receiver responds with a FIN + ACK, confirming the termination
 *
 * Selective Repeat (SR) Support:
 *    - The TCU protocol employs Selective Repeat (SR) ARQ to ensure reliable data transmission
 *    - SR allows retransmission of only corrupted or lost fragments based on the NACK packets
 *
 * Flags Combinations :
 * 1. Connection Request — SYN, LEN 0
 * 2. Connection Acknowledgment — SYN + ACK, LEN 0
 *
 * 3. Disconnection Request — FIN, LEN 0
 * 4. Disconnection Acknowledgment — FIN + ACK, LEN 0
 *
 * 5. Single Message — DF, LEN
 * 6. Fragment of Message — MF, LEN
 * 7. Last Fragment of Message — NONE, LEN
 *
 * 8. Single File - DF + FL, LEN
 * 9. Fragment of File — MF + FL, LEN
 * 10. Last Fragment of File — FL, LEN
 *
 * 11. Keep-Alive — KA, LEN 0
 *
 * 12. Acknowledgment - ACK, LEN 0, SEQ NUM 0
 * 13. Negative Acknowledgment — NACK, LEN 0, SEQ NUM [ERR FRG]
 */

#pragma once

#include <cstdint>
#include <vector>
#include <iostream>
#include <cstring>
#include <netinet/in.h>
#include <chrono>

#define TCU_PHASE_DEAD          0
#define TCU_PHASE_HOLDOFF       1
#define TCU_PHASE_INITIALIZE    2
#define TCU_PHASE_CONNECT       3
#define TCU_PHASE_NETWORK       4
#define TCU_PHASE_DISCONNECT    5
#define TCU_PHASE_CLOSED        6

#define TCU_HDR_FLAG_SYN        0x01
#define TCU_HDR_FLAG_ACK        0x02
#define TCU_HDR_FLAG_FIN        0x04
#define TCU_HDR_FLAG_NACK       0x08
#define TCU_HDR_FLAG_DF         0x10
#define TCU_HDR_FLAG_MF         0x20
#define TCU_HDR_FLAG_FL         0x40
#define TCU_HDR_FLAG_KA         0x80

#define TCU_HDR_LEN             8
#define TCU_MAX_PAYLOAD         65499

struct tcu_header {
    uint16_t length;            // Payload length
    uint16_t flags;             // Flags
    uint16_t seq_number;        // Sequence packet number
    uint16_t checksum;          // CRC sum
};

struct tcu_packet {
    tcu_header header;
    unsigned char* payload;

    unsigned char* to_buff();
    static tcu_packet from_buff(unsigned char* buff);

    void calculate_crc();
    bool validate_crc() ;
};

uint16_t calculate_crc16(unsigned char* data, size_t length);   // CRC16-CCITT algorithm

/* TCU PCB (Protocol Control Block) */
struct tcu_pcb {
    /* Connection params */
    uint8_t phase = TCU_PHASE_DEAD;     // Phase, where link at
    std::vector< tcu_packet > window;   // Window for Selective Repeat (SR)

    /* Source node params */
    uint16_t src_port;

    /* Destination node params */
    uint16_t dest_port;
    in_addr dest_ip;
    struct sockaddr_in dest_addr;

    void new_phase(int phase);
};