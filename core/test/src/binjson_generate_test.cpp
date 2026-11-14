#include "includes.hpp"

namespace binjson_generate_test {

#include "binjson.hpp"
#include "binjson.cpp"

void appendUint32(std::vector<uint8_t>& bytes, uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void appendUint64(std::vector<uint8_t>& bytes, uint64_t value)
{
    for (int shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void appendDouble(std::vector<uint8_t>& bytes, double value)
{
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    appendUint64(bytes, bits);
}

std::vector<uint8_t> encodedBytes(const Buffer& buffer)
{
    auto* data = static_cast<const uint8_t*>(buffer);
    return std::vector<uint8_t>(data, data + buffer.size());
}

void expectGenerated(const std::shared_ptr<BinJSON::Value>& value,
    const std::vector<uint8_t>& expected, std::vector<Buffer>* generatedBuffers = nullptr)
{
    std::vector<Buffer> buffers;
    buffers.resize(1);
    Buffer output(0);
    BinJSON::generate(value, output, 0, buffers, 1, 1);
    buffers[0] = output;

    ASSERT_FALSE(buffers.empty());
    EXPECT_EQ(encodedBytes(buffers[0]), expected);
    if (generatedBuffers != nullptr) {
        *generatedBuffers = std::move(buffers);
    }
}

TEST(BinJSONGenerateTest, GeneratesNull)
{
    expectGenerated(BinJSON::from(nullptr), {0});
}

TEST(BinJSONGenerateTest, GeneratesBinaryAndStoresItsBuffer)
{
    Buffer source(3);
    auto* sourceData = static_cast<uint8_t*>(source);
    sourceData[0] = 0x12;
    sourceData[1] = 0x34;
    sourceData[2] = 0x56;

    std::vector<uint8_t> expected{1};
    appendUint32(expected, 1);
    std::vector<Buffer> buffers;
    expectGenerated(BinJSON::from(source), expected, &buffers);

    ASSERT_EQ(buffers.size(), 2u);
    EXPECT_EQ(encodedBytes(buffers[1]), (std::vector<uint8_t>{0x12, 0x34, 0x56}));
}

TEST(BinJSONGenerateTest, GeneratesNumber)
{
    std::vector<uint8_t> expected{2};
    appendDouble(expected, 12.5);

    expectGenerated(BinJSON::from(12.5), expected);
}

TEST(BinJSONGenerateTest, GeneratesLengthDelimitedString)
{
    const std::string text("longer than one byte");
    std::vector<uint8_t> expected{3};
    appendUint32(expected, static_cast<uint32_t>(text.size()));
    expected.insert(expected.end(), text.begin(), text.end());

    expectGenerated(BinJSON::from(text), expected);
}

TEST(BinJSONGenerateTest, GeneratesArrayWithNestedValues)
{
    std::vector<std::shared_ptr<BinJSON::Value>> entries{
        BinJSON::from(nullptr),
        BinJSON::from(7.0),
        BinJSON::from(std::string("ok")),
    };

    std::vector<uint8_t> payload{0, 2};
    appendDouble(payload, 7.0);
    payload.push_back(3);
    appendUint32(payload, 2);
    payload.insert(payload.end(), {'o', 'k'});

    std::vector<uint8_t> expected{4};
    appendUint32(expected, static_cast<uint32_t>(sizeof(uint32_t) + payload.size()));
    appendUint32(expected, static_cast<uint32_t>(entries.size()));
    expected.insert(expected.end(), payload.begin(), payload.end());

    expectGenerated(BinJSON::from(entries), expected);
}

TEST(BinJSONGenerateTest, GeneratesObjectWithNamedProperties)
{
    auto text = BinJSON::from(std::string("value"));
    auto number = BinJSON::from(3.25);
    std::map<std::string, std::shared_ptr<BinJSON::Value>> properties{
        {"a", number},
        {"long", text},
    };

    std::vector<uint8_t> payload;
    appendUint32(payload, 1);
    payload.push_back('a');
    payload.push_back(2);
    appendDouble(payload, 3.25);
    appendUint32(payload, 4);
    payload.insert(payload.end(), {'l', 'o', 'n', 'g'});
    payload.push_back(3);
    appendUint32(payload, 5);
    payload.insert(payload.end(), {'v', 'a', 'l', 'u', 'e'});

    std::vector<uint8_t> expected{5};
    appendUint32(expected, static_cast<uint32_t>(sizeof(uint32_t) + payload.size()));
    appendUint32(expected, static_cast<uint32_t>(properties.size()));
    expected.insert(expected.end(), payload.begin(), payload.end());

    expectGenerated(BinJSON::from(properties), expected);
}

}
