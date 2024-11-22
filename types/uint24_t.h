/*
 * uint24_t — Unsigned Integer Type of 24 Bits (3 Bytes) Width Realization
 *
 * Author: Anton Dmitriev
 */

#pragma once

#include <cstdint>
#include <arpa/inet.h>

#pragma pack(push, 1)   // Not padding to 4 bytes
class uint24_t {
private:
    uint32_t value : 24;

public:
    /* Constructors */
    uint24_t();
    uint24_t(uint32_t v);

    /* Overriding = */
    uint24_t& operator=(uint32_t v);

    operator uint32_t() const;

    /* Overriding arithmetics */
    uint24_t& operator+=(uint32_t v);
    uint24_t& operator-=(uint32_t v);
    uint24_t& operator*=(uint32_t v);
    uint24_t& operator/=(uint32_t v);
    uint24_t& operator%=(uint32_t v);

    uint24_t operator+(uint32_t v) const;
    uint24_t operator-(uint32_t v) const;
    uint24_t operator*(uint32_t v) const;
    uint24_t operator/(uint32_t v) const;
    uint24_t operator%(uint32_t v) const;

    uint24_t& operator++();
    uint24_t operator++(int);
    uint24_t& operator--();
    uint24_t operator--(int);

    /* Overriding logic */
    bool operator==(const uint24_t& other) const;
    bool operator==(const int& other) const;
    bool operator!=(const uint24_t& other) const;
    bool operator<(const uint24_t& other) const;
    bool operator<=(const uint24_t& other) const;
    bool operator>(const uint24_t& other) const;
    bool operator>=(const uint24_t& other) const;
};
#pragma pack(pop)

static_assert(sizeof(uint24_t) == 3, "size of uint24_t not 3 bytes");

/* Convert to network byte order */
uint24_t hton24(uint24_t host24);
uint24_t ntoh24(uint24_t net24);