#include "value.h"

#include <cstdio>

#include "wire.h"

namespace ro {

namespace {

constexpr uint8_t kTagNull = 0;
constexpr uint8_t kTagFalse = 1;
constexpr uint8_t kTagTrue = 2;
constexpr uint8_t kTagInt = 3;
constexpr uint8_t kTagDouble = 4;
constexpr uint8_t kTagString = 5;
constexpr uint8_t kTagBytes = 6;
constexpr uint8_t kTagList = 7;
constexpr uint8_t kTagMap = 8;
constexpr uint8_t kTagStruct = 9;

std::string numberToString(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

}  // namespace

Value Value::bytes(std::string b)
{
    Value v;
    v.type_ = ValueType::Bytes;
    v.str_ = std::move(b);
    return v;
}

Value Value::list(List l)
{
    Value v;
    v.type_ = ValueType::List;
    v.list_ = std::move(l);
    return v;
}

Value Value::map(Map m)
{
    Value v;
    v.type_ = ValueType::Map;
    v.map_ = std::move(m);
    return v;
}

Value Value::structure(std::string typeName, List fields)
{
    Value v;
    v.type_ = ValueType::Struct;
    v.str_ = std::move(typeName);
    v.list_ = std::move(fields);
    return v;
}

bool Value::toBool() const
{
    switch (type_) {
    case ValueType::Null: return false;
    case ValueType::Bool: return bool_;
    case ValueType::Int: return int_ != 0;
    case ValueType::Double: return double_ != 0;
    case ValueType::String:
    case ValueType::Bytes: return !str_.empty();
    case ValueType::List: return !list_.empty();
    case ValueType::Map: return !map_.empty();
    case ValueType::Struct: return !list_.empty();
    }
    return false;
}

int64_t Value::toInt() const
{
    switch (type_) {
    case ValueType::Bool: return bool_ ? 1 : 0;
    case ValueType::Int: return int_;
    case ValueType::Double: return static_cast<int64_t>(double_);
    default: return 0;
    }
}

double Value::toDouble() const
{
    switch (type_) {
    case ValueType::Bool: return bool_ ? 1.0 : 0.0;
    case ValueType::Int: return static_cast<double>(int_);
    case ValueType::Double: return double_;
    default: return 0.0;
    }
}

bool Value::operator==(const Value &o) const
{
    if (type_ != o.type_)
        return false;
    switch (type_) {
    case ValueType::Null: return true;
    case ValueType::Bool: return bool_ == o.bool_;
    case ValueType::Int: return int_ == o.int_;
    case ValueType::Double: return double_ == o.double_;
    case ValueType::String:
    case ValueType::Bytes: return str_ == o.str_;
    case ValueType::List: return list_ == o.list_;
    case ValueType::Map: return map_ == o.map_;
    case ValueType::Struct: return str_ == o.str_ && list_ == o.list_;
    }
    return false;
}

std::string Value::debugString() const
{
    switch (type_) {
    case ValueType::Null: return "null";
    case ValueType::Bool: return bool_ ? "true" : "false";
    case ValueType::Int: return std::to_string(int_);
    case ValueType::Double: return numberToString(double_);
    case ValueType::String: return "\"" + str_ + "\"";
    case ValueType::Bytes: return "bytes(" + std::to_string(str_.size()) + ")";
    case ValueType::List: {
        std::string out = "[";
        for (size_t i = 0; i < list_.size(); ++i) {
            if (i)
                out += ", ";
            out += list_[i].debugString();
        }
        return out + "]";
    }
    case ValueType::Map: {
        std::string out = "{";
        for (size_t i = 0; i < map_.size(); ++i) {
            if (i)
                out += ", ";
            out += map_[i].first.debugString() + ": " + map_[i].second.debugString();
        }
        return out + "}";
    }
    case ValueType::Struct: {
        std::string out = str_ + "{";
        for (size_t i = 0; i < list_.size(); ++i) {
            if (i)
                out += ", ";
            out += list_[i].debugString();
        }
        return out + "}";
    }
    }
    return "?";
}

void encodeValue(Writer &w, const Value &v)
{
    switch (v.type()) {
    case ValueType::Null:
        w.u8(kTagNull);
        break;
    case ValueType::Bool:
        w.u8(v.toBool() ? kTagTrue : kTagFalse);
        break;
    case ValueType::Int:
        w.u8(kTagInt);
        w.svarint(v.toInt());
        break;
    case ValueType::Double:
        w.u8(kTagDouble);
        w.f64(v.toDouble());
        break;
    case ValueType::String:
        w.u8(kTagString);
        w.bytes(v.str());
        break;
    case ValueType::Bytes:
        w.u8(kTagBytes);
        w.bytes(v.str());
        break;
    case ValueType::List:
        w.u8(kTagList);
        w.varint(v.list().size());
        for (const auto &item : v.list())
            encodeValue(w, item);
        break;
    case ValueType::Map:
        w.u8(kTagMap);
        w.varint(v.map().size());
        for (const auto &entry : v.map()) {
            encodeValue(w, entry.first);
            encodeValue(w, entry.second);
        }
        break;
    case ValueType::Struct:
        w.u8(kTagStruct);
        w.bytes(v.structName());
        w.varint(v.fields().size());
        for (const auto &field : v.fields())
            encodeValue(w, field);
        break;
    }
}

bool decodeValue(Reader &r, Value &v)
{
    const uint8_t tag = r.u8();
    if (!r.ok())
        return false;
    switch (tag) {
    case kTagNull:
        v = Value();
        return true;
    case kTagFalse:
        v = Value(false);
        return true;
    case kTagTrue:
        v = Value(true);
        return true;
    case kTagInt: {
        const int64_t i = r.svarint();
        if (!r.ok())
            return false;
        v = Value(i);
        return true;
    }
    case kTagDouble: {
        const double d = r.f64();
        if (!r.ok())
            return false;
        v = Value(d);
        return true;
    }
    case kTagString: {
        const auto s = r.bytes();
        if (!r.ok())
            return false;
        v = Value(std::string(s));
        return true;
    }
    case kTagBytes: {
        const auto s = r.bytes();
        if (!r.ok())
            return false;
        v = Value::bytes(std::string(s));
        return true;
    }
    case kTagList: {
        const uint64_t n = r.varint();
        if (!r.ok())
            return false;
        Value::List items;
        for (uint64_t i = 0; i < n; ++i) {
            Value item;
            if (!decodeValue(r, item))
                return false;
            items.push_back(std::move(item));
        }
        v = Value::list(std::move(items));
        return true;
    }
    case kTagMap: {
        const uint64_t n = r.varint();
        if (!r.ok())
            return false;
        Value::Map entries;
        for (uint64_t i = 0; i < n; ++i) {
            Value key;
            Value value;
            if (!decodeValue(r, key) || !decodeValue(r, value))
                return false;
            entries.emplace_back(std::move(key), std::move(value));
        }
        v = Value::map(std::move(entries));
        return true;
    }
    case kTagStruct: {
        const auto name = r.bytes();
        if (!r.ok())
            return false;
        const uint64_t n = r.varint();
        if (!r.ok())
            return false;
        Value::List fields;
        for (uint64_t i = 0; i < n; ++i) {
            Value field;
            if (!decodeValue(r, field))
                return false;
            fields.push_back(std::move(field));
        }
        v = Value::structure(std::string(name), std::move(fields));
        return true;
    }
    default:
        return false;
    }
}

}  // namespace ro
