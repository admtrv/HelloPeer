/*
 * uint24_t.cpp
 */

#include "uint24_t.h"

uint24_t::uint24_t() : value(0) {}

uint24_t::uint24_t(uint32_t v) : value(v & 0xFFFFFF) {}

uint24_t& uint24_t::operator=(uint32_t v) {
    value = v & 0xFFFFFF;
    return *this;
}

uint24_t::operator uint32_t() const {
    return value;
}

uint24_t& uint24_t::operator+=(uint32_t v){
    value = (value + v) & 0xFFFFFF;
    return *this;
}

uint24_t& uint24_t::operator-=(uint32_t v) {
    value = (value - v) & 0xFFFFFF;
    return *this;
}

uint24_t& uint24_t::operator*=(uint32_t v) {
    value = (value * v) & 0xFFFFFF;
    return *this;
}

uint24_t& uint24_t::operator/=(uint32_t v) {
    value /= v;
    return *this;
}

uint24_t& uint24_t::operator%=(uint32_t v) {
    value %= v;
    return *this;
}

uint24_t uint24_t::operator+(uint32_t v) const {
    return uint24_t((value + v) & 0xFFFFFF);
}

uint24_t uint24_t::operator-(uint32_t v) const {
    return uint24_t((value - v) & 0xFFFFFF);
}

uint24_t uint24_t::operator*(uint32_t v) const {
    return uint24_t((value * v) & 0xFFFFFF);
}

uint24_t uint24_t::operator/(uint32_t v) const {
    return uint24_t(value / v);
}

uint24_t uint24_t::operator%(uint32_t v) const {
    return uint24_t(value % v);
}

uint24_t& uint24_t::operator++() {
    value = (value + 1) & 0xFFFFFF;
    return *this;
}

uint24_t uint24_t::operator++(int) {
    uint24_t temp = *this;
    ++(*this);
    return temp;
}

uint24_t& uint24_t::operator--() {
    value = (value - 1) & 0xFFFFFF;
    return *this;
}

uint24_t uint24_t::operator--(int) {
    uint24_t temp = *this;
    --(*this);
    return temp;
}

bool uint24_t::operator==(const uint24_t& other) const {
    return value == other.value;
}

bool uint24_t::operator==(const int& other) const {
    return static_cast<uint32_t>(*this) == static_cast<uint32_t>(other);
}

bool uint24_t::operator!=(const uint24_t& other) const {
    return value != other.value;
}

bool uint24_t::operator<(const uint24_t& other) const {
    return value < other.value;
}

bool uint24_t::operator<=(const uint24_t& other) const {
    return value <= other.value;
}

bool uint24_t::operator>(const uint24_t& other) const {
    return value > other.value;
}

bool uint24_t::operator>=(const uint24_t& other) const {
    return value >= other.value;
}

uint24_t hton24(uint24_t host24)
{
    uint32_t host_value = static_cast<uint32_t>(host24) & 0xFFFFFF;

    uint32_t net_value = ((host_value & 0x0000FF) << 16) |
                         (host_value & 0x00FF00) |
                         ((host_value & 0xFF0000) >> 16);

    return uint24_t(net_value);
}

uint24_t ntoh24(uint24_t net24)
{
    uint32_t net_value = static_cast<uint32_t>(net24) & 0xFFFFFF;

    uint32_t host_value = ((net_value & 0x0000FF) << 16) |
                          (net_value & 0x00FF00) |
                          ((net_value & 0xFF0000) >> 16);

    return uint24_t(host_value);
}