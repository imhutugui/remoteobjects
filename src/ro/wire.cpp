#include "wire.h"

#include <cstring>

namespace ro {

void Writer::u16(uint16_t v)
{
    u8(static_cast<uint8_t>(v & 0xff));
    u8(static_cast<uint8_t>((v >> 8) & 0xff));
}

void Writer::u32(uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        u8(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}

void Writer::u64(uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        u8(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}

void Writer::varint(uint64_t v)
{
    while (v >= 0x80) {
        u8(static_cast<uint8_t>(v) | 0x80);
        v >>= 7;
    }
    u8(static_cast<uint8_t>(v));
}

void Writer::svarint(int64_t v)
{
    // zigzag
    varint((static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63));
}

void Writer::f64(double v)
{
    uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    u64(bits);
}

void Writer::bytes(std::string_view s)
{
    varint(s.size());
    raw(s);
}

uint8_t Reader::u8()
{
    if (!take(1))
        return 0;
    return static_cast<uint8_t>(view_[pos_ - 1]);
}

uint16_t Reader::u16()
{
    uint16_t v = 0;
    for (int i = 0; i < 2; ++i)
        v |= static_cast<uint16_t>(u8()) << (8 * i);
    return v;
}

uint32_t Reader::u32()
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= static_cast<uint32_t>(u8()) << (8 * i);
    return v;
}

uint64_t Reader::u64()
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(u8()) << (8 * i);
    return v;
}

uint64_t Reader::varint()
{
    uint64_t result = 0;
    int shift = 0;
    while (true) {
        if (shift > 63) {
            ok_ = false;
            return 0;
        }
        const uint8_t byte = u8();
        if (!ok_)
            return 0;
        result |= static_cast<uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0)
            break;
        shift += 7;
    }
    return result;
}

int64_t Reader::svarint()
{
    const uint64_t v = varint();
    return static_cast<int64_t>((v >> 1) ^ (~(v & 1) + 1));
}

double Reader::f64()
{
    const uint64_t bits = u64();
    double v = 0;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

std::string_view Reader::raw(size_t n)
{
    if (!take(n))
        return {};
    const std::string_view out = view_.substr(pos_ - n, n);
    return out;
}

std::string_view Reader::bytes()
{
    const uint64_t n = varint();
    if (!ok_ || n > remaining()) {
        ok_ = false;
        return {};
    }
    return raw(static_cast<size_t>(n));
}

bool Reader::take(size_t n)
{
    if (!ok_ || remaining() < n) {
        ok_ = false;
        return false;
    }
    pos_ += n;
    return true;
}

}  // namespace ro
