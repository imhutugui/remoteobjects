// remoteobject - dynamic value type used on the wire and in property caches.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ro {

class Writer;
class Reader;

enum class ValueType : uint8_t { Null = 0, Bool, Int, Double, String, Bytes, List, Map, Struct };

// A self-describing value. Every remote property, signal argument, method
// argument and return value travels as a Value.
class Value {
public:
    using List = std::vector<Value>;
    using Map = std::vector<std::pair<Value, Value>>;

    Value() = default;
    Value(std::nullptr_t) noexcept {}

    Value(bool v) : type_(ValueType::Bool), bool_(v) {}

    template <class T,
              std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    Value(T v) : type_(ValueType::Int), int_(static_cast<int64_t>(v))
    {
    }

    template <class T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
    Value(T v) : type_(ValueType::Int), int_(static_cast<int64_t>(v))
    {
    }

    template <class T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    Value(T v) : type_(ValueType::Double), double_(static_cast<double>(v))
    {
    }

    Value(const char *s) : type_(ValueType::String), str_(s) {}
    Value(std::string s) : type_(ValueType::String), str_(std::move(s)) {}
    Value(std::string_view s) : type_(ValueType::String), str_(s) {}

    static Value bytes(std::string b);
    static Value list(List l);
    static Value map(Map m);
    static Value structure(std::string typeName, List fields);

    ValueType type() const { return type_; }
    bool isNull() const { return type_ == ValueType::Null; }

    bool toBool() const;
    int64_t toInt() const;
    double toDouble() const;

    // String and Bytes values.
    const std::string &str() const { return str_; }
    const List &list() const { return list_; }
    const Map &map() const { return map_; }
    const std::string &structName() const { return str_; }
    const List &fields() const { return list_; }

    bool operator==(const Value &o) const;
    bool operator!=(const Value &o) const { return !(*this == o); }

    std::string debugString() const;

private:
    ValueType type_ = ValueType::Null;
    bool bool_ = false;
    int64_t int_ = 0;
    double double_ = 0;
    std::string str_;  // String/Bytes payload, or struct type name
    List list_;        // List items, or Struct fields
    Map map_;          // Map entries
};

void encodeValue(Writer &w, const Value &v);
bool decodeValue(Reader &r, Value &v);

template <class T> struct is_vector : std::false_type {};
template <class T, class A> struct is_vector<std::vector<T, A>> : std::true_type {};

template <class T> struct is_map : std::false_type {};
template <class K, class V, class C, class A> struct is_map<std::map<K, V, C, A>> : std::true_type {};

// C++ -> Value. User defined structs (generated for PODs) provide toValue().
template <class T> Value toValue(const T &v)
{
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, Value>) {
        return v;
    } else if constexpr (std::is_same_v<U, bool>) {
        return Value(v);
    } else if constexpr (std::is_enum_v<U>) {
        return Value(static_cast<int64_t>(v));
    } else if constexpr (std::is_integral_v<U>) {
        return Value(static_cast<int64_t>(v));
    } else if constexpr (std::is_floating_point_v<U>) {
        return Value(static_cast<double>(v));
    } else if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, const char *>
                         || std::is_same_v<U, char *> || std::is_same_v<U, std::string_view>) {
        return Value(std::string(v));
    } else if constexpr (is_vector<U>::value) {
        typename Value::List out;
        out.reserve(v.size());
        for (const auto &item : v)
            out.push_back(toValue(item));
        return Value::list(std::move(out));
    } else if constexpr (is_map<U>::value) {
        typename Value::Map out;
        out.reserve(v.size());
        for (const auto &entry : v)
            out.emplace_back(toValue(entry.first), toValue(entry.second));
        return Value::map(std::move(out));
    } else if constexpr (requires(const U &u) { u.toValue(); }) {
        return v.toValue();
    } else {
        static_assert(!sizeof(T), "ro::toValue: unsupported type");
    }
}

// Value -> C++. User defined structs provide a static fromValue().
template <class T> T fromValue(const Value &v)
{
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, Value>) {
        return v;
    } else if constexpr (std::is_same_v<U, bool>) {
        return v.toBool();
    } else if constexpr (std::is_enum_v<U>) {
        return static_cast<U>(v.toInt());
    } else if constexpr (std::is_integral_v<U>) {
        return static_cast<U>(v.toInt());
    } else if constexpr (std::is_floating_point_v<U>) {
        return static_cast<U>(v.toDouble());
    } else if constexpr (std::is_same_v<U, std::string>) {
        return std::string(v.str());
    } else if constexpr (is_vector<U>::value) {
        U out;
        out.reserve(v.list().size());
        for (const auto &item : v.list())
            out.push_back(fromValue<typename U::value_type>(item));
        return out;
    } else if constexpr (is_map<U>::value) {
        U out;
        for (const auto &entry : v.map())
            out.emplace(fromValue<typename U::key_type>(entry.first),
                        fromValue<typename U::mapped_type>(entry.second));
        return out;
    } else if constexpr (requires { U::fromValue(v); }) {
        return U::fromValue(v);
    } else {
        static_assert(!sizeof(T), "ro::fromValue: unsupported type");
    }
}

}  // namespace ro
