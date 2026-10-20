// remoteobject - byte level encoding helpers and framing.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ro {

// Little endian writer. Integers use zigzag varints so small values stay small.
class Writer {
public:
    void u8(uint8_t v) { buf_.push_back(static_cast<char>(v)); }
    void u16(uint16_t v);
    void u32(uint32_t v);
    void u64(uint64_t v);
    void varint(uint64_t v);
    void svarint(int64_t v);
    void f64(double v);
    void raw(std::string_view s) { buf_.append(s); }
    void bytes(std::string_view s);

    std::string &data() { return buf_; }
    const std::string &data() const { return buf_; }
    std::string take() { return std::move(buf_); }
    void clear() { buf_.clear(); }
    size_t size() const { return buf_.size(); }

private:
    std::string buf_;
};

class Reader {
public:
    explicit Reader(std::string_view v) : view_(v) {}

    bool ok() const { return ok_; }
    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    uint64_t varint();
    int64_t svarint();
    double f64();
    std::string_view raw(size_t n);
    std::string_view bytes();

    bool atEnd() const { return pos_ >= view_.size(); }
    size_t remaining() const { return view_.size() - pos_; }

private:
    bool take(size_t n);

    std::string_view view_;
    size_t pos_ = 0;
    bool ok_ = true;
};

}  // namespace ro
