/*
 * tcu.cpp
 */

#include "tcu.h"

tcu_packet::tcu_packet() : payload(nullptr) {}

tcu_packet::tcu_packet(const tcu_packet& other)
{
    header = other.header;
    if (other.payload != nullptr && header.length > 0)
    {
        payload = new unsigned char[header.length];
        std::memcpy(payload, other.payload, header.length);
    }
    else
    {
        payload = nullptr;
    }
}

tcu_packet& tcu_packet::operator=(const tcu_packet& other)
{
    if (this == &other)
        return *this;

    if (payload != nullptr)
    {
        delete[] payload;
        payload = nullptr;
    }

    header = other.header;
    if (other.payload != nullptr && header.length > 0)
    {
        payload = new unsigned char[header.length];
        std::memcpy(payload, other.payload, header.length);
    }
    else
    {
        payload = nullptr;
    }

    return *this;
}

tcu_packet::~tcu_packet()
{
    if (payload != nullptr)
    {
        delete[] payload;
        payload = nullptr;
    }
}

unsigned char* tcu_packet::to_buff()
{
    size_t total_size = sizeof(tcu_header) + header.length;

    unsigned char* buffer = new unsigned char[total_size];
    size_t offset = 0;

    // Length
    std::memcpy(buffer + offset, &header.length, sizeof(header.length));
    offset += sizeof(header.length);

    // Flags
    std::memcpy(buffer + offset, &header.flags, sizeof(header.flags));
    offset += sizeof(header.flags);

    // Sequence Number
    std::memcpy(buffer + offset, &header.seq_number, sizeof(header.seq_number));
    offset += sizeof(header.seq_number);

    // Checksum
    std::memcpy(buffer + offset, &header.checksum, sizeof(header.checksum));
    offset += sizeof(header.checksum);

    // Payload
    std::memcpy(buffer + offset, payload, header.length);

    return buffer;
}

tcu_packet tcu_packet::from_buff(unsigned char* buff)
{
    tcu_packet packet{};

    size_t offset = 0;

    // Length
    std::memcpy(&packet.header.length, buff + offset, sizeof(packet.header.length));
    offset += sizeof(packet.header.length);

    // Flags
    std::memcpy(&packet.header.flags, buff + offset, sizeof(packet.header.flags));
    offset += sizeof(packet.header.flags);

    // Sequence Number
    std::memcpy(&packet.header.seq_number, buff + offset, sizeof(packet.header.seq_number));
    offset += sizeof(packet.header.seq_number);

    // Checksum
    std::memcpy(&packet.header.checksum, buff + offset, sizeof(packet.header.checksum));
    offset += sizeof(packet.header.checksum);

    // Payload
    packet.payload = new unsigned char[packet.header.length];
    std::memcpy(packet.payload, buff + offset, packet.header.length);

    return packet;
}

uint16_t calculate_crc16(const unsigned char* data, size_t length)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= (data[i] << 8);

        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void tcu_packet::calculate_crc()
{
    size_t total_size = sizeof(tcu_header) - sizeof(header.checksum) + header.length;
    unsigned char* buffer = new unsigned char[total_size];

    // Copy header without CRC
    std::memcpy(buffer, &header, sizeof(tcu_header) - sizeof(header.checksum));

    // Copy payload
    std::memcpy(buffer + sizeof(tcu_header) - sizeof(header.checksum), payload, header.length);

    // Calculate CRC
    header.checksum = calculate_crc16(buffer, total_size);

    delete[] buffer;
}

bool tcu_packet::validate_crc()
{
    size_t total_size = sizeof(tcu_header) - sizeof(header.checksum) + header.length;
    unsigned char* buffer = new unsigned char[total_size];

    // Copy header without CRC
    std::memcpy(buffer, &header, sizeof(tcu_header) - sizeof(header.checksum));

    // Copy payload
    std::memcpy(buffer + sizeof(tcu_header) - sizeof(header.checksum), payload, header.length);

    // Calculate CRC
    uint16_t computed_crc = calculate_crc16(buffer, total_size);

    delete[] buffer;

    return computed_crc == header.checksum;
}

void tcu_pcb::new_phase(int new_phase)
{
    if (phase >= TCU_PHASE_DEAD && phase <= TCU_PHASE_CLOSED)
    {
        phase = new_phase;
        spdlog::info("[tcu_pcb::new_phase] new phase {}", int(phase));
    }
    else
    {
        spdlog::error("[tcu_pcb::new_phase] unknown phase {}", int(phase));
    }
}

void tcu_pcb::update_last_activity()
{
    std::lock_guard<std::mutex> lock(activity_mutex);
    last_activity.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
}

bool tcu_pcb::is_activity_recent() const
{
    std::lock_guard<std::mutex> lock(activity_mutex);
    auto last = last_activity.load(std::memory_order_relaxed);
    auto now = std::chrono::steady_clock::now();
    return (now - last) < std::chrono::seconds(TCU_ACTIVITY_ATTEMPT_COUNT * TCU_ACTIVITY_ATTEMPT_INTERVAL);
}

void tcu_pcb::set_max_frag_size(size_t size)
{
    max_frag_size = size;
    spdlog::info("[tcu_pcb::set_max_frag_size] set max fragment size {}", size);
}