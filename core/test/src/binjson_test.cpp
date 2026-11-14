#include "includes.hpp"

namespace binjson_test {

#include "binjson.hpp"
#include "binjson.cpp"

TEST(BinJSONTypeTest, TypeTagsMatchBinJSONEncoding)
{
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::NUL), 0);
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::BINARY), 1);
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::NUMBER), 2);
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::STRING), 3);
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::ARRAY), 4);
    EXPECT_EQ(static_cast<uint8_t>(BinJSON::BinJSONType::OBJECT), 5);
}

TEST(BinJSONValueTest, NullHasNullTypeAndFlag)
{
    BinJSON::Null value;

    EXPECT_EQ(value.getType(), BinJSON::BinJSONType::NUL);
    EXPECT_TRUE(value.isNull());
}

TEST(BinJSONValueTest, BinaryReturnsItsBuffer)
{
    Buffer buffer(2);
    auto* data = static_cast<uint8_t*>(buffer);
    data[0] = 0x12;
    data[1] = 0x34;
    BinJSON::Binary value(buffer);

    auto result = value.buffer();
    auto* resultData = static_cast<uint8_t*>(result);
    EXPECT_EQ(value.getType(), BinJSON::BinJSONType::BINARY);
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(resultData[0], 0x12);
    EXPECT_EQ(resultData[1], 0x34);
}

TEST(BinJSONValueTest, NumberHasTypeAndReturnsItsValue)
{
    BinJSON::Number value(12.5);

    EXPECT_EQ(value.getType(), BinJSON::BinJSONType::NUMBER);
    EXPECT_DOUBLE_EQ(value.number(), 12.5);
    EXPECT_TRUE(std::isnan(BinJSON::Null().number()));
}

TEST(BinJSONValueTest, StringSupportsLengthDelimitedInput)
{
    BinJSON::String value("hello", 3);

    EXPECT_EQ(value.getType(), BinJSON::BinJSONType::STRING);
    EXPECT_EQ(value.string(), "hel");
}

TEST(BinJSONValueTest, ToReturnsSelfOnMatchingTypeAndDefaultOtherwise)
{
    auto value = std::make_shared<BinJSON::Number>(12.5);

    EXPECT_EQ(value->to<BinJSON::Number>(), value);
    auto otherType = value->to<BinJSON::String>();
    EXPECT_EQ(otherType->getType(), BinJSON::BinJSONType::STRING);
    EXPECT_TRUE(otherType->string().empty());
}

TEST(BinJSONArrayTest, SupportsLengthResizeAndIndexedAccess)
{
    BinJSON::Array value(1);
    auto entry = BinJSON::from(7.0);
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
    BinJSON::Object value;

    auto& missing = value["missing"];
    ASSERT_NE(missing, nullptr);
    EXPECT_TRUE(missing->isNull());
    EXPECT_EQ(value.length(), 1);

    auto entry = BinJSON::from(7.0);
    value["present"] = entry;
    EXPECT_EQ(value.length(), 2);
    EXPECT_EQ(value.get("present"), entry);
}

TEST(BinJSONFactoryTest, CreatesValuesFromSupportedInputs)
{
    Buffer buffer(1);
    static_cast<uint8_t*>(buffer)[0] = 0xA5;
    auto nullValue = BinJSON::from(nullptr);
    auto Binary = BinJSON::from(buffer);
    auto numberValue = BinJSON::from(2.5);
    auto stringValue = BinJSON::from(std::string("text"));
    std::vector<std::shared_ptr<BinJSON::Value>> entries{numberValue};
    auto arrayValue = BinJSON::from(entries);
    std::map<std::string, std::shared_ptr<BinJSON::Value>> properties{
        {"text", stringValue}};
    auto objectValue = BinJSON::from(properties);

    EXPECT_TRUE(nullValue->isNull());
    EXPECT_EQ(Binary->buffer().size(), 1);
    EXPECT_DOUBLE_EQ(numberValue->number(), 2.5);
    EXPECT_EQ(stringValue->string(), "text");
    EXPECT_EQ(arrayValue->get(0), numberValue);
    EXPECT_EQ(objectValue->get("text"), stringValue);
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
