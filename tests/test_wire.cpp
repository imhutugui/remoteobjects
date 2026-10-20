#include "ro/protocol.h"
#include "ro/value.h"
#include "ro/wire.h"
#include "test.h"

using namespace ro;

namespace {

Value roundTrip(const Value &v)
{
    Writer w;
    encodeValue(w, v);
    Reader r(w.data());
    Value out;
    const bool ok = decodeValue(r, out);
    RO_CHECK(ok);
    RO_CHECK(r.ok());
    RO_CHECK(r.atEnd());
    return out;
}

}  // namespace

RO_TEST(value_primitives_roundtrip)
{
    RO_CHECK_EQ(roundTrip(Value()), Value());
    RO_CHECK_EQ(roundTrip(Value(true)), Value(true));
    RO_CHECK_EQ(roundTrip(Value(false)), Value(false));
    RO_CHECK_EQ(roundTrip(Value(int64_t{-1234567890123})), Value(int64_t{-1234567890123}));
    RO_CHECK_EQ(roundTrip(Value(3.5)), Value(3.5));
    RO_CHECK_EQ(roundTrip(Value("hello")), Value("hello"));
    RO_CHECK_EQ(roundTrip(Value::bytes(std::string("\x00\x01\x02", 3))),
                Value::bytes(std::string("\x00\x01\x02", 3)));
}

RO_TEST(value_small_ints_use_one_byte)
{
    Writer w;
    encodeValue(w, Value(int64_t{1}));
    RO_CHECK_EQ(w.size(), size_t{2});  // tag + zigzag byte
}

RO_TEST(value_containers_roundtrip)
{
    Value::List items{Value(1), Value("two"), Value(3.0)};
    RO_CHECK_EQ(roundTrip(Value::list(items)), Value::list(items));

    Value::Map entries{{Value("a"), Value(1)}, {Value(2), Value("b")}};
    RO_CHECK_EQ(roundTrip(Value::map(entries)), Value::map(entries));

    Value nested = Value::list({Value::list({Value(1)}), Value::map({{Value("k"), Value(2)}})});
    RO_CHECK_EQ(roundTrip(nested), nested);

    Value pod = Value::structure("PresetInfo", {Value(1), Value(98.5), Value("station")});
    RO_CHECK_EQ(roundTrip(pod), pod);
}

RO_TEST(value_typed_conversion_helpers)
{
    RO_CHECK_EQ(toValue(true), Value(true));
    RO_CHECK_EQ(toValue(42), Value(42));
    RO_CHECK_EQ(toValue(std::string("x")), Value("x"));
    RO_CHECK_EQ(toValue(std::vector<int>{1, 2}), Value::list({Value(1), Value(2)}));

    std::map<std::string, int> m{{"a", 1}, {"b", 2}};
    RO_CHECK_EQ(toValue(m), Value::map({{Value("a"), Value(1)}, {Value("b"), Value(2)}}));

    RO_CHECK_EQ(fromValue<int>(Value(7)), 7);
    RO_CHECK_EQ(fromValue<std::string>(Value("s")), std::string("s"));
    RO_CHECK_EQ(fromValue<std::vector<int>>(Value::list({Value(1), Value(2)})),
                (std::vector<int>{1, 2}));
    using IntMap = std::map<std::string, int>;
    const IntMap converted = fromValue<IntMap>(Value::map({{Value("a"), Value(1)}}));
    RO_CHECK_EQ(converted, (IntMap{{"a", 1}}));
}

RO_TEST(reader_rejects_truncated_data)
{
    Writer w;
    encodeValue(w, Value("a string long enough"));
    const std::string full = w.take();
    Reader r(std::string_view(full).substr(0, full.size() - 3));
    Value v;
    decodeValue(r, v);
    RO_CHECK(!r.ok());
}

RO_TEST(frame_parser_handles_split_and_coalesced_reads)
{
    const std::string a = makeFrame(PacketType::Ping, "A");
    const std::string b = makeFrame(PacketType::Pong, "BC");

    FrameParser parser;
    PacketType type = PacketType::Invalid;
    std::string_view body;

    // One byte at a time.
    size_t consumed = 0;
    int frames = 0;
    for (char c : a + b) {
        parser.append(std::string_view(&c, 1));
        while (parser.next(type, body)) {
            ++frames;
            if (frames == 1) {
                RO_CHECK(type == PacketType::Ping);
                RO_CHECK_EQ(std::string(body), std::string("A"));
            } else {
                RO_CHECK(type == PacketType::Pong);
                RO_CHECK_EQ(std::string(body), std::string("BC"));
            }
            consumed += body.size();
        }
    }
    RO_CHECK_EQ(frames, 2);
    RO_CHECK_EQ(consumed, size_t{3});
    RO_CHECK_EQ(parser.buffered(), size_t{0});
}

RO_TEST(frame_parser_accepts_both_frames_in_one_read)
{
    FrameParser parser;
    parser.append(makeFrame(PacketType::Acquire, "obj") + makeFrame(PacketType::RemoveObject, "obj"));
    PacketType type = PacketType::Invalid;
    std::string_view body;
    RO_CHECK(parser.next(type, body));
    RO_CHECK(type == PacketType::Acquire);
    RO_CHECK(parser.next(type, body));
    RO_CHECK(type == PacketType::RemoveObject);
    RO_CHECK(!parser.next(type, body));
}
