#include "includes.hpp"

namespace binjson_test {

#include "binjson.hpp"
#include "binjson.cpp"

TEST(BinJSONTypeTest, TypeTagsMatchBinJSONEncoding)
{
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::NUL), 0);
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::BINARY), 1);
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::BOOLEAN), 2);
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::NUMBER), 3);
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::STRING), 4);
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::ARRAY), 5);
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::OBJECT), 6);
}

TEST(BinJSONValueTest, NullHasNullTypeAndFlag)
{
    BinJSON::NullValue value;

    EXPECT_EQ(value.getType(), BinJSON::BinJSONType::NUL);
    EXPECT_TRUE(value.isNull());
}

TEST(BinJSONValueTest, BinaryReturnsItsBuffer)
{
    Buffer<> buffer(2);
    auto* data = buffer.raw<uint8_t*>();
    data[0] = 0x12;
    data[1] = 0x34;
    BinJSON::BinaryValue value(buffer);

    auto result = value.buffer();
    auto* resultData = result.raw<uint8_t*>();
    EXPECT_EQ(value.getType(), BinJSON::BinJSONType::BINARY);
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(resultData[0], 0x12);
    EXPECT_EQ(resultData[1], 0x34);
}

TEST(BinJSONValueTest, BinaryReturnsItsBufferAsRequestedType)
{
    Buffer<> buffer(sizeof(uint32_t));
    *buffer.raw<uint32_t*>() = 0x12345678;
    BinJSON::Any value = BinJSON::makeBinary(buffer);

    auto typedBuffer = value->buffer<uint32_t>();

    EXPECT_EQ(typedBuffer.raw<uint32_t*>(), buffer.raw<uint32_t*>());
    EXPECT_EQ(*typedBuffer, 0x12345678U);
}

TEST(BinJSONValueTest, BinaryRetainsBufferAfterSourceIsDestroyed)
{
    BinJSON::Any value;
    {
        Buffer<> buffer(2);
        buffer.raw<uint8_t*>()[0] = 0x12;
        buffer.raw<uint8_t*>()[1] = 0x34;
        value = binJSON(buffer);
    }

    auto result = value->buffer();
    value.reset();
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result.raw<uint8_t*>()[0], 0x12);
    EXPECT_EQ(result.raw<uint8_t*>()[1], 0x34);
}

TEST(BinJSONValueTest, BooleanHasTypeAndReturnsItsValue)
{
    BinJSON::BooleanValue falseValue;
    BinJSON::BooleanValue trueValue(true);

    EXPECT_EQ(falseValue.getType(), BinJSON::BinJSONType::BOOLEAN);
    EXPECT_FALSE(falseValue.boolean());
    EXPECT_EQ(trueValue.getType(), BinJSON::BinJSONType::BOOLEAN);
    EXPECT_TRUE(trueValue.boolean());
    EXPECT_FALSE(BinJSON::NumberValue(1).boolean());
}

TEST(BinJSONValueTest, NumberHasTypeAndReturnsItsValue)
{
    BinJSON::NumberValue value(12.5);

    EXPECT_EQ(value.getType(), BinJSON::BinJSONType::NUMBER);
    EXPECT_DOUBLE_EQ(value.number(), 12.5);
    EXPECT_TRUE(std::isnan(BinJSON::NullValue().number()));
}

TEST(BinJSONValueTest, StringSupportsLengthDelimitedInput)
{
    BinJSON::StringValue value("hello", 3);

    EXPECT_EQ(value.getType(), BinJSON::BinJSONType::STRING);
    EXPECT_EQ(value.string(), "hel");
}

TEST(BinJSONValueTest, ErrorStoresAndExposesItsMessage)
{
    BinJSON::ErrorValue empty;
    BinJSON::ErrorValue cStringError("invalid input");
    BinJSON::ErrorValue stringError(std::string("parse failed"));

    EXPECT_EQ(empty.getType(), BinJSON::BinJSONType::ERROR);
    EXPECT_TRUE(empty.isError());
    EXPECT_TRUE(empty.error().empty());
    EXPECT_EQ(cStringError.error(), "invalid input");
    EXPECT_EQ(stringError.error(), "parse failed");

    stringError.error() = "updated message";
    EXPECT_EQ(stringError.message, "updated message");
}

TEST(BinJSONValueTest, ToReturnsSelfOnMatchingTypeAndDefaultOtherwise)
{
    auto value = BinJSON::makeNumber(12.5);

    EXPECT_EQ(value->to<BinJSON::NumberValue>(), value);
    auto otherType = value->to<BinJSON::StringValue>();
    EXPECT_EQ(otherType->getType(), BinJSON::BinJSONType::STRING);
    EXPECT_TRUE(otherType->string().empty());
}

TEST(BinJSONArrayTest, SupportsLengthResizeAndIndexedAccess)
{
    BinJSON::ArrayValue value(1);
    auto entry = binJSON(7.0);
    value[0] = entry;

    ASSERT_EQ(value.length(), 1);
    EXPECT_EQ(value.get(0), entry);

    value.length(2);
    EXPECT_EQ(value.length(), 2);
    EXPECT_EQ(value.get(-1)->getType(), BinJSON::BinJSONType::NUL);
    EXPECT_EQ(value.get(2)->getType(), BinJSON::BinJSONType::NUL);
}

TEST(BinJSONObjectTest, MissingPropertiesAreInsertedAsNull)
{
    BinJSON::ObjectValue value;

    auto& missing = value["missing"];
    ASSERT_NE(missing, nullptr);
    EXPECT_TRUE(missing->isNull());
    EXPECT_EQ(value.length(), 1);

    auto entry = binJSON(7.0);
    value["present"] = entry;
    EXPECT_EQ(value.length(), 2);
    EXPECT_EQ(value.get("present"), entry);
}

TEST(BinJSONFactoryTest, CreatesValuesFromAllOverloads)
{
    auto nullValue = binJSON(nullptr);
    auto booleanValue = binJSON(true);
    auto integerValue = binJSON(7);
    auto numberValue = binJSON(2.5);
    auto cStringValue = binJSON("c string");
    auto stringValue = binJSON(std::string("text"));
    Buffer<uint16_t> buffer(sizeof(uint16_t));
    *buffer.raw<uint16_t*>() = 0xA55A;
    auto binaryValue = binJSON(buffer);
    std::vector<BinJSON::Any> vectorEntries{integerValue, numberValue};
    auto vectorValue = binJSON(vectorEntries);
    std::array<BinJSON::Any, 2> arrayEntries{cStringValue, stringValue};
    auto arrayValue = binJSON(arrayEntries);
    std::map<std::string, BinJSON::Any> properties{
        {"array", arrayValue},
        {"text", stringValue}};
    auto objectValue = binJSON(properties);

    EXPECT_TRUE(nullValue->isNull());
    EXPECT_TRUE(booleanValue->boolean());
    EXPECT_EQ(booleanValue->getType(), BinJSON::BinJSONType::BOOLEAN);
    EXPECT_DOUBLE_EQ(integerValue->number(), 7.0);
    EXPECT_DOUBLE_EQ(numberValue->number(), 2.5);
    EXPECT_EQ(cStringValue->string(), "c string");
    EXPECT_EQ(stringValue->string(), "text");
    EXPECT_EQ(binaryValue->buffer<uint16_t>().raw<uint16_t*>(), buffer.raw<uint16_t*>());
    ASSERT_EQ(vectorValue->length(), 2);
    EXPECT_EQ(vectorValue->get(0), integerValue);
    EXPECT_EQ(vectorValue->get(1), numberValue);
    ASSERT_EQ(arrayValue->length(), 2);
    EXPECT_EQ(arrayValue->get(0), cStringValue);
    EXPECT_EQ(arrayValue->get(1), stringValue);
    EXPECT_EQ(objectValue->get("array"), arrayValue);
    EXPECT_EQ(objectValue->get("text"), stringValue);
}

TEST(BinJSONArrayFactoryTest, CreatesArrayFromPointerAndSize)
{
    std::array<BinJSON::Any, 2> entries{binJSON(7), binJSON("text")};

    auto value = binJSONArray(entries.data(), static_cast<int32_t>(entries.size()));

    ASSERT_EQ(value->length(), 2);
    EXPECT_EQ(value->get(0), entries[0]);
    EXPECT_EQ(value->get(1), entries[1]);
}

TEST(BinJSONObjectFactoryTest, CreatesObjectFromInitializerList)
{
    auto text = binJSON("text");
    auto count = binJSON(2);

    auto value = binJSONObject({
        {"text", text},
        {"count", count},
    });

    ASSERT_EQ(value->length(), 2);
    EXPECT_EQ(value->get("text"), text);
    EXPECT_EQ(value->get("count"), count);
}

TEST(BinJSONLiteralTest, CreatesNumberAndStringValues)
{
    auto integer = 42_binjson;
    auto decimal = 1.5_binjson;
    auto text = "hi"_binjson;

    EXPECT_DOUBLE_EQ(integer->number(), 42.0);
    EXPECT_DOUBLE_EQ(decimal->number(), 1.5);
    EXPECT_EQ(text->string(), "hi");
}

}
