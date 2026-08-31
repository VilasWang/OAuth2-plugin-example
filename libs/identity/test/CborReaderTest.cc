// #142 (WebAuthn real-signature verification): unit tests for CborReader,
// the limits-enforcing wrapper over libcbor's cbor_load(). CBOR inputs are
// hand-written hex literals (annotated per RFC 8949 initial-byte layout)
// so the tests never depend on an encoder to produce their fixtures, plus
// programmatically built buffers for the size/depth limits.

#include "../src/webauthn/CborReader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

using fulla::identity::webauthn::CborReader;

namespace
{

// "a1b2" -> "\xa1\xb2"; also accepts lower-case digits.
std::string fromHex(const std::string &hex)
{
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    std::string bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        bytes.push_back(static_cast<char>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    return bytes;
}

}  // namespace

// ---------------------------------------------------------------------------
// Positive: scalar items
// ---------------------------------------------------------------------------

TEST(CborReaderTest, UintScalars)
{
    // 0x0a = initial byte for unsigned 10 (major 0, value in low 5 bits).
    std::string error;
    auto reader = CborReader::load(fromHex("0a"), &error);
    ASSERT_TRUE(reader.has_value()) << error;
    EXPECT_TRUE(reader->root().isUint());
    EXPECT_TRUE(reader->root().isInt());
    EXPECT_FALSE(reader->root().isNint());
    EXPECT_EQ(reader->root().asInt(), 10);
}

TEST(CborReaderTest, Uint300TwoByteArgument)
{
    // 0x19 0x012c = unsigned 300 encoded with the 2-byte argument form.
    auto reader = CborReader::load(fromHex("19012c"));
    ASSERT_TRUE(reader.has_value());
    EXPECT_EQ(reader->root().asInt(), 300);
}

TEST(CborReaderTest, NegintScalars)
{
    // 0x26 = negative -7 (major 1, magnitude 6, value -1-6).
    auto minus7 = CborReader::load(fromHex("26"));
    ASSERT_TRUE(minus7.has_value());
    EXPECT_TRUE(minus7->root().isNint());
    EXPECT_TRUE(minus7->root().isInt());
    EXPECT_FALSE(minus7->root().isUint());
    EXPECT_EQ(minus7->root().asInt(), -7);

    // 0x39 0x03e7 = -1000 (magnitude 999, 2-byte argument).
    auto minus1000 = CborReader::load(fromHex("3903e7"));
    ASSERT_TRUE(minus1000.has_value());
    EXPECT_EQ(minus1000->root().asInt(), -1000);
}

TEST(CborReaderTest, Int64Boundaries)
{
    // 0x1b ffffffffffffffff = uint64 max -> parses but does not fit int64_t.
    auto tooBig = CborReader::load(fromHex("1bffffffffffffffff"));
    ASSERT_TRUE(tooBig.has_value());
    EXPECT_FALSE(tooBig->root().asInt().has_value());

    // 0x3b 7fffffffffffffff = magnitude 2^63-1 -> exactly INT64_MIN.
    auto int64Min = CborReader::load(fromHex("3b7fffffffffffffff"));
    ASSERT_TRUE(int64Min.has_value());
    EXPECT_EQ(int64Min->root().asInt(), INT64_MIN);

    // magnitude 2^63 -> one below INT64_MIN, unrepresentable.
    auto belowMin = CborReader::load(fromHex("3b8000000000000000"));
    ASSERT_TRUE(belowMin.has_value());
    EXPECT_FALSE(belowMin->root().asInt().has_value());
}

TEST(CborReaderTest, BoolAndNull)
{
    auto trueItem = CborReader::load(fromHex("f5"));  // simple(21) = true
    ASSERT_TRUE(trueItem.has_value());
    EXPECT_TRUE(trueItem->root().isBool());

    auto falseItem = CborReader::load(fromHex("f4"));  // simple(20) = false
    ASSERT_TRUE(falseItem.has_value());
    EXPECT_TRUE(falseItem->root().isBool());

    auto nullItem = CborReader::load(fromHex("f6"));  // simple(22) = null
    ASSERT_TRUE(nullItem.has_value());
    EXPECT_TRUE(nullItem->root().isNull());
    EXPECT_FALSE(nullItem->root().isBool());
    EXPECT_FALSE(nullItem->root().isInt());
}

TEST(CborReaderTest, TextAndByteStrings)
{
    // 0x65 "hello" = text string of 5 bytes.
    auto text = CborReader::load(fromHex("6568656c6c6f"));
    ASSERT_TRUE(text.has_value());
    EXPECT_TRUE(text->root().isString());
    EXPECT_FALSE(text->root().isBytes());
    EXPECT_EQ(text->root().asString(), "hello");
    EXPECT_FALSE(text->root().asBytes().has_value());  // wrong-type access

    // 0x43 010203 = byte string of 3 bytes.
    auto binary = CborReader::load(fromHex("43010203"));
    ASSERT_TRUE(binary.has_value());
    EXPECT_TRUE(binary->root().isBytes());
    EXPECT_FALSE(binary->root().isString());
    EXPECT_EQ(binary->root().asBytes(), std::string("\x01\x02\x03", 3));
    EXPECT_FALSE(binary->root().asString().has_value());
}

// ---------------------------------------------------------------------------
// Positive: containers
// ---------------------------------------------------------------------------

TEST(CborReaderTest, MapLookupAndIndexing)
{
    // a2                          map(2)
    //   61 61 01                  "a": uint 1
    //   61 62 82 02 03            "b": array[uint 2, uint 3]
    auto reader = CborReader::load(fromHex("a26161016162820203"));
    ASSERT_TRUE(reader.has_value());
    const auto root = reader->root();
    ASSERT_TRUE(root.isMap());
    EXPECT_EQ(root.mapSize(), 2u);

    const auto one = root.mapLookup("a");
    ASSERT_TRUE(one.has_value());
    EXPECT_TRUE(one->isUint());
    EXPECT_EQ(one->asInt(), 1);

    const auto nested = root.mapLookup("b");
    ASSERT_TRUE(nested.has_value());
    EXPECT_TRUE(nested->isArray());
    EXPECT_EQ(nested->arraySize(), 2u);
    EXPECT_EQ(nested->arrayAt(0)->asInt(), 2);
    EXPECT_EQ(nested->arrayAt(1)->asInt(), 3);

    EXPECT_FALSE(root.mapLookup("missing").has_value());

    const auto pair0 = root.mapAt(0);
    ASSERT_TRUE(pair0.has_value());
    EXPECT_EQ(pair0->first.asString(), "a");
    EXPECT_EQ(pair0->second.asInt(), 1);
}

TEST(CborReaderTest, WrongTypeAccessorsReturnNullopt)
{
    // array[uint 1] used against map-only accessors, and vice versa.
    auto array = CborReader::load(fromHex("8101"));
    ASSERT_TRUE(array.has_value());
    EXPECT_FALSE(array->root().mapSize().has_value());
    EXPECT_FALSE(array->root().mapLookup("x").has_value());
    EXPECT_FALSE(array->root().mapAt(0).has_value());
    EXPECT_FALSE(array->root().arrayAt(1).has_value());  // out of range

    // map(0): empty map, mapAt(0) is out of range.
    auto emptyMap = CborReader::load(fromHex("a0"));
    ASSERT_TRUE(emptyMap.has_value());
    EXPECT_EQ(emptyMap->root().mapSize(), 0u);
    EXPECT_FALSE(emptyMap->root().mapAt(0).has_value());
    EXPECT_FALSE(emptyMap->root().arraySize().has_value());
}

TEST(CborReaderTest, DepthEightIsAccepted)
{
    // 8 nested arrays around uint 10: depth exactly at the limit.
    auto reader = CborReader::load(fromHex("8181818181818181") + fromHex("0a"));
    ASSERT_TRUE(reader.has_value());
    auto node = reader->root();
    for (int depth = 0; depth < 8; ++depth)
    {
        EXPECT_TRUE(node.isArray()) << "level " << depth;
        node = *node.arrayAt(0);
    }
    EXPECT_EQ(node.asInt(), 10);
}

TEST(CborReaderTest, DoubleFloatParsesAsNonInt)
{
    // 0xfb 3ff199999999999a = IEEE-754 double 1.1 (major 7, 8-byte payload).
    auto reader = CborReader::load(fromHex("fb3ff199999999999a"));
    ASSERT_TRUE(reader.has_value());
    EXPECT_FALSE(reader->root().isInt());
    EXPECT_FALSE(reader->root().asString().has_value());
}

// ---------------------------------------------------------------------------
// Negative: malformed / over-limit inputs
// ---------------------------------------------------------------------------

TEST(CborReaderTest, EmptyInputRejected)
{
    std::string error;
    EXPECT_FALSE(CborReader::load("", &error).has_value());
    EXPECT_FALSE(error.empty());
}

TEST(CborReaderTest, GarbageBytesRejected)
{
    std::string error;
    // 0xff 0xfe: 0xff is the "break" stop code, invalid as an item start.
    EXPECT_FALSE(CborReader::load(fromHex("fffe"), &error).has_value());
    EXPECT_FALSE(error.empty());

    // Reserved additional-info 28 (0x1c) as an initial byte.
    EXPECT_FALSE(CborReader::load(fromHex("1c"), &error).has_value());
    EXPECT_FALSE(error.empty());
}

TEST(CborReaderTest, TruncatedInputsRejected)
{
    // uint 300's 2-byte argument cut to one byte.
    EXPECT_FALSE(CborReader::load(fromHex("1901")).has_value());
    // text(4) with only 2 payload bytes.
    EXPECT_FALSE(CborReader::load(fromHex("644142")).has_value());
    // array(2) with one element.
    EXPECT_FALSE(CborReader::load(fromHex("8201")).has_value());
    // tag(0) with no tagged item after it.
    EXPECT_FALSE(CborReader::load(fromHex("c0")).has_value());
}

TEST(CborReaderTest, TrailingBytesRejected)
{
    std::string error;
    // Two uint items in one buffer: load() accepts exactly one item.
    EXPECT_FALSE(CborReader::load(fromHex("0102"), &error).has_value());
    EXPECT_NE(error.find("trailing"), std::string::npos);
}

TEST(CborReaderTest, DepthNineRejected)
{
    std::string error;
    // 9 nested arrays around uint 10: one past the limit.
    EXPECT_FALSE(CborReader::load(fromHex("818181818181818181") + fromHex("0a"), &error).has_value());
    EXPECT_NE(error.find("nesting"), std::string::npos);
}

TEST(CborReaderTest, OversizeStringRejected)
{
    std::string error;
    // 0x7a 00010001 = text string claiming 65537 bytes (limit: 65536).
    std::string head = fromHex("7a00010001");
    std::string oversized = head + std::string(65537, 'a');
    EXPECT_FALSE(CborReader::load(oversized, &error).has_value());
    EXPECT_NE(error.find("65536"), std::string::npos);

    // Exactly 65536 is still fine.
    EXPECT_TRUE(CborReader::load(head.replace(1, 4, fromHex("00010000")) + std::string(65536, 'a')).has_value());
}

TEST(CborReaderTest, OversizeInputRejected)
{
    std::string error;
    // A structurally valid text string of total size 131073 (limit: 131072):
    // 5-byte header (0x7a + 4-byte length 0x00020004) + 131068 payload.
    std::string oversized = fromHex("7a00020004") + std::string(131068, 'z');
    ASSERT_EQ(oversized.size(), 131073u);
    EXPECT_FALSE(CborReader::load(oversized, &error).has_value());
    EXPECT_NE(error.find("131072"), std::string::npos);
}

TEST(CborReaderTest, TooManyItemsRejected)
{
    std::string error;
    // array(1025) of uint 1 -> 1026 items including the array itself.
    std::string tooMany = fromHex("990401") + std::string(1025, '\x01');
    EXPECT_FALSE(CborReader::load(tooMany, &error).has_value());
    EXPECT_NE(error.find("too many"), std::string::npos);

    // array(1023) of uint 1 -> exactly 1024 items, at the limit.
    EXPECT_TRUE(CborReader::load(fromHex("9903ff") + std::string(1023, '\x01')).has_value());
}

TEST(CborReaderTest, IndefiniteLengthItemsRejected)
{
    std::string error;
    // 0x9f 01 ff = indefinite-length array [1] with a break terminator.
    EXPECT_FALSE(CborReader::load(fromHex("9f01ff"), &error).has_value());
    EXPECT_NE(error.find("indefinite"), std::string::npos);

    // 0xbf 6161 01 ff = indefinite-length map {"a": 1}.
    EXPECT_FALSE(CborReader::load(fromHex("bf616101ff"), &error).has_value());

    // 0x7f 6161 ff = indefinite text string with one definite chunk "a".
    EXPECT_FALSE(CborReader::load(fromHex("7f6161ff"), &error).has_value());

    // 0x5f 420102 ff = indefinite byte string with one chunk 0x0102.
    EXPECT_FALSE(CborReader::load(fromHex("5f420102ff"), &error).has_value());
}

// ---------------------------------------------------------------------------
// measure()
// ---------------------------------------------------------------------------

TEST(CborReaderTest, MeasureReturnsFirstItemLength)
{
    std::string error;
    // array[2,3] encodes to 3 bytes.
    EXPECT_EQ(CborReader::measure(fromHex("820203").data(), 3, &error), 3u);
    EXPECT_TRUE(error.empty());

    // uint 1 followed by an unrelated byte: only the first item counts.
    EXPECT_EQ(CborReader::measure(fromHex("0102").data(), 2, &error), 1u);

    // Text with 2-byte argument: 3-byte header + 300 payload = 303.
    std::string longText = fromHex("79012c") + std::string(300, 'x');
    EXPECT_EQ(CborReader::measure(longText.data(), longText.size(), &error), 303u);
}

TEST(CborReaderTest, MeasureAppliesSameLimits)
{
    std::string error;
    EXPECT_FALSE(CborReader::measure("", 0, &error).has_value());           // empty
    EXPECT_FALSE(CborReader::measure(fromHex("9f01ff").data(), 3, &error)  // indefinite
                 .has_value());
    EXPECT_FALSE(CborReader::measure(fromHex("1901").data(), 2, &error)  // truncated
                 .has_value());

    // Depth 9 nested arrays.
    const std::string deep = fromHex("818181818181818181") + fromHex("0a");
    EXPECT_FALSE(CborReader::measure(deep.data(), deep.size(), &error).has_value());
}
