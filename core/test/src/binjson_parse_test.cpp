#include "includes.hpp"

namespace binjson_parse_test {

#include "binjson.hpp"
#include "binjson.cpp"

void appendUint32(std::vector<uint8_t>& bytes, uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void appendDouble(std::vector<uint8_t>& bytes, double value)
{
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<uint8_t>(bits >> shift));
    }
}

Buffer<> makeBuffer(const std::vector<uint8_t>& bytes)
{
    Buffer<> buffer(static_cast<int32_t>(bytes.size()));
    if (!bytes.empty()) {
        std::memcpy(buffer.raw<uint8_t*>(), bytes.data(), bytes.size());
    }
    return buffer;
}

TEST(BinJSONParseTest, ParsesValuesGeneratedByGenerator)
{
    std::vector<std::shared_ptr<BinJSON::Value>> entries{
        BinJSON::from(nullptr),
        BinJSON::from(-2.75),
        BinJSON::from(std::string("hello")),
        BinJSON::from(std::string("")),
    };
    auto object = BinJSON::from(std::map<std::string,
        std::shared_ptr<BinJSON::Value>>{
            {"array", BinJSON::from(entries)},
            {"null", BinJSON::from(nullptr)},
        });

    std::vector<Buffer<>> encoded;
    BinJSON::generate(object, encoded);
    auto parsed = BinJSON::parse(encoded);

    ASSERT_FALSE(parsed->isError()) << parsed->error();
    ASSERT_EQ(parsed->getType(), BinJSON::BinJSONType::OBJECT);
    auto parsedObject = parsed->to<BinJSON::Object>();
    ASSERT_EQ(parsedObject->length(), 2);
    ASSERT_EQ(parsedObject->get("array")->getType(), BinJSON::BinJSONType::ARRAY);
    auto parsedArray = parsedObject->get("array")->to<BinJSON::Array>();
    ASSERT_EQ(parsedArray->length(), 4);
    EXPECT_TRUE(parsedArray->get(0)->isNull());
    EXPECT_DOUBLE_EQ(parsedArray->get(1)->number(), -2.75);
    EXPECT_EQ(parsedArray->get(2)->string(), "hello");
    EXPECT_EQ(parsedArray->get(3)->string(), "");
    EXPECT_TRUE(parsedObject->get("null")->isNull());
}

TEST(BinJSONParseTest, ResolvesBinaryBuffersUsingConfiguredOffsets)
{
    Buffer<> payload(3);
    auto* data = payload.raw<uint8_t*>();
    data[0] = 0x12;
    data[1] = 0x34;
    data[2] = 0x56;
    auto binary = BinJSON::from(payload);
    Buffer<> binjson(0);
    std::vector<Buffer<>> input;
    BinJSON::generate(binary, binjson, 0, input, 2, 7);

    ASSERT_EQ(input.size(), 3u);
    EXPECT_EQ(binjson.raw<uint8_t*>()[0], static_cast<uint8_t>(BinJSON::BinJSONType::BINARY));
    EXPECT_EQ(binjson.raw<uint8_t*>()[1], 7);
    auto parsed = BinJSON::parse(binjson, 0, input, 2, 7);

    ASSERT_FALSE(parsed->isError()) << parsed->error();
    ASSERT_EQ(parsed->getType(), BinJSON::BinJSONType::BINARY);
    auto parsedBuffer = parsed->buffer();
    EXPECT_EQ(parsedBuffer.size(), 3);
    EXPECT_EQ(parsedBuffer.raw<uint8_t*>()[0], 0x12);
    EXPECT_EQ(parsedBuffer.raw<uint8_t*>()[1], 0x34);
    EXPECT_EQ(parsedBuffer.raw<uint8_t*>()[2], 0x56);
}

TEST(BinJSONParseTest, SupportsBufferOffset)
{
    std::vector<uint8_t> bytes{0xFF, 2};
    appendDouble(bytes, 42.5);
    auto parsed = BinJSON::parse(makeBuffer(bytes), 1, {}, 0, 0);

    ASSERT_FALSE(parsed->isError()) << parsed->error();
    EXPECT_DOUBLE_EQ(parsed->number(), 42.5);
}

TEST(BinJSONParseTest, ParsesEmptyContainersAndNestedStructures)
{
    auto emptyArray = BinJSON::parse(makeBuffer({4, 4, 0, 0, 0, 0, 0, 0, 0}), 0, {}, 0, 0);
    auto emptyObject = BinJSON::parse(makeBuffer({5, 4, 0, 0, 0, 0, 0, 0, 0}), 0, {}, 0, 0);

    ASSERT_FALSE(emptyArray->isError()) << emptyArray->error();
    ASSERT_FALSE(emptyObject->isError()) << emptyObject->error();
    EXPECT_EQ(emptyArray->getType(), BinJSON::BinJSONType::ARRAY);
    EXPECT_EQ(emptyArray->length(), 0);
    EXPECT_EQ(emptyObject->getType(), BinJSON::BinJSONType::OBJECT);
    EXPECT_EQ(emptyObject->length(), 0);

    std::vector<std::shared_ptr<BinJSON::Value>> children{
        BinJSON::from(std::string("inner")),
        BinJSON::from(std::vector<std::shared_ptr<BinJSON::Value>>{
            BinJSON::from(nullptr),
        }),
    };
    std::vector<Buffer<>> encoded;
    BinJSON::generate(BinJSON::from(children), encoded);
    auto parsed = BinJSON::parse(encoded);

    ASSERT_FALSE(parsed->isError()) << parsed->error();
    auto array = parsed->to<BinJSON::Array>();
    ASSERT_EQ(array->length(), 2);
    EXPECT_EQ(array->get(0)->string(), "inner");
    ASSERT_EQ(array->get(1)->getType(), BinJSON::BinJSONType::ARRAY);
    EXPECT_TRUE(array->get(1)->to<BinJSON::Array>()->get(0)->isNull());
}

TEST(BinJSONParseTest, PreservesBinaryStringsAndObjectNames)
{
    const std::string binaryString("a\0b", 3);
    std::map<std::string, std::shared_ptr<BinJSON::Value>> values{
        {std::string("key\0name", 8), BinJSON::from(binaryString)},
    };
    std::vector<Buffer<>> encoded;
    BinJSON::generate(BinJSON::from(values), encoded);

    auto parsed = BinJSON::parse(encoded);
    ASSERT_FALSE(parsed->isError()) << parsed->error();
    auto object = parsed->to<BinJSON::Object>();
    ASSERT_EQ(object->length(), 1);
    EXPECT_EQ(object->get(std::string("key\0name", 8))->string(), binaryString);
}

TEST(BinJSONParseTest, RejectsInvalidAndTruncatedInput)
{
    EXPECT_TRUE(BinJSON::parse(std::vector<Buffer<>>{})->isError());
    EXPECT_TRUE(BinJSON::parse(makeBuffer({} ), 0, {}, 0, 0)->isError());
    auto unknownType = BinJSON::parse(makeBuffer({99}), 0, {}, 0, 0);
    ASSERT_TRUE(unknownType->isError());
    EXPECT_EQ(unknownType->getType(), BinJSON::BinJSONType::ERROR);
    EXPECT_EQ(unknownType->error(), "Unknown binJSON type");
    EXPECT_TRUE(BinJSON::parse(makeBuffer({2, 0, 0}), 0, {}, 0, 0)->isError());
    EXPECT_TRUE(BinJSON::parse(makeBuffer({1, 0, 0, 0, 0}), 0, {}, 0, 0)->isError());
}

TEST(BinJSONParseTest, RejectsOversizedStringLengths)
{
    std::vector<uint8_t> truncatedString{3};
    appendUint32(truncatedString, 4);
    truncatedString.push_back('x');
    auto truncated = BinJSON::parse(makeBuffer(truncatedString), 0, {}, 0, 0);
    ASSERT_TRUE(truncated->isError());
    EXPECT_EQ(truncated->error(), "Unexpected end of binJSON data");

    std::vector<uint8_t> impossibleString{3};
    appendUint32(impossibleString, UINT32_MAX);
    auto impossible = BinJSON::parse(makeBuffer(impossibleString), 0, {}, 0, 0);
    ASSERT_TRUE(impossible->isError());
    EXPECT_EQ(impossible->error(), "Invalid binJSON string length");
}

TEST(BinJSONParseTest, RejectsInvalidContainerSizesAndCounts)
{
    std::vector<uint8_t> badSize{4};
    appendUint32(badSize, 3);
    appendUint32(badSize, 0);
    EXPECT_TRUE(BinJSON::parse(makeBuffer(badSize), 0, {}, 0, 0)->isError());

    std::vector<uint8_t> badCount{4};
    appendUint32(badCount, 4);
    appendUint32(badCount, 1);
    EXPECT_TRUE(BinJSON::parse(makeBuffer(badCount), 0, {}, 0, 0)->isError());

    std::vector<uint8_t> trailingData{4};
    appendUint32(trailingData, 5);
    appendUint32(trailingData, 0);
    trailingData.push_back(0xAA);
    auto trailing = BinJSON::parse(makeBuffer(trailingData), 0, {}, 0, 0);
    ASSERT_TRUE(trailing->isError());
    EXPECT_EQ(trailing->error(), "BinJSON array size does not match its contents");

    std::vector<uint8_t> invalidNameLength{5};
    appendUint32(invalidNameLength, 9);
    appendUint32(invalidNameLength, 1);
    appendUint32(invalidNameLength, UINT32_MAX);
    invalidNameLength.push_back(0);
    auto invalidName = BinJSON::parse(makeBuffer(invalidNameLength), 0, {}, 0, 0);
    ASSERT_TRUE(invalidName->isError());
    EXPECT_EQ(invalidName->error(), "Invalid binJSON object property name length");
}

TEST(BinJSONParseTest, RejectsMissingBinaryBuffersAndInvalidOffsets)
{
    auto binary = makeBuffer({1, 0, 0, 0, 1});
    EXPECT_TRUE(BinJSON::parse(binary, 0, {}, 0, 0)->isError());
    EXPECT_TRUE(BinJSON::parse(binary, -1, {}, 0, 0)->isError());
    EXPECT_TRUE(BinJSON::parse(binary, 0, {}, -1, 0)->isError());
    EXPECT_TRUE(BinJSON::parse(binary, 0, {}, 0, -1)->isError());
    EXPECT_TRUE(BinJSON::parse(binary, binary.size() + 1, {}, 0, 0)->isError());

    std::vector<uint8_t> encodedIndexBeforeBase{1};
    appendUint32(encodedIndexBeforeBase, 1);
    EXPECT_TRUE(BinJSON::parse(makeBuffer(encodedIndexBeforeBase), 0, {Buffer<>(0)}, 0, 2)->isError());
}

TEST(BinJSONParseTest, RejectsExcessiveNesting)
{
    std::vector<uint8_t> nested{0};
    for (int32_t i = 0; i < 257; ++i) {
        std::vector<uint8_t> parent{4};
        appendUint32(parent, static_cast<uint32_t>(sizeof(uint32_t) + nested.size()));
        appendUint32(parent, 1);
        parent.insert(parent.end(), nested.begin(), nested.end());
        nested = std::move(parent);
    }

    auto parsed = BinJSON::parse(makeBuffer(nested), 0, {}, 0, 0);
    ASSERT_TRUE(parsed->isError());
    EXPECT_EQ(parsed->error(), "Maximum binJSON nesting depth exceeded");
}

}
