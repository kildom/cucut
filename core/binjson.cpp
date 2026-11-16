

#include "binjson.hpp"

namespace BinJSON {

namespace {

static constexpr int32_t maximumParseDepth = 256;

static std::shared_ptr<Value> parseError(const char* message)
{
    return std::make_shared<Error>(message);
}

static bool readUint32(const uint8_t* data, int32_t& offset, int32_t limit,
    uint32_t& value)
{
    if (offset < 0 || offset > limit
        || static_cast<int64_t>(limit) - offset < static_cast<int32_t>(sizeof(uint32_t))) {
        return false;
    }
    std::memcpy(&value, data + offset, sizeof(value));
    offset += sizeof(value);
    return true;
}

static bool hasBytes(int32_t offset, int32_t count, int32_t limit)
{
    return offset >= 0 && count >= 0 && offset <= limit && count <= limit - offset;
}

static std::shared_ptr<Value> parseValue(const uint8_t* data, int32_t& offset,
    int32_t limit, const std::vector<Buffer<>>& array, int32_t arrayOffset,
    int32_t bufferIndex, int32_t depth)
{
    if (depth > maximumParseDepth) {
        return parseError("Maximum binJSON nesting depth exceeded");
    }
    if (!hasBytes(offset, 1, limit)) {
        return parseError("Unexpected end of binJSON data");
    }
    auto type = static_cast<BinJSONType>(data[offset++]);

    switch (type)
    {
    case BinJSONType::NUL:
        return std::make_shared<Null>();
    case BinJSONType::BINARY: {
        uint32_t encodedIndex;
        if (!readUint32(data, offset, limit, encodedIndex)) {
            return parseError("Unexpected end of binJSON data");
        }
        int64_t index = static_cast<int64_t>(encodedIndex)
            - static_cast<int64_t>(bufferIndex) + static_cast<int64_t>(arrayOffset);
        if (index < 0 || static_cast<uint64_t>(index) >= array.size()) {
            return parseError("Binary buffer index is out of range");
        }
        return std::make_shared<Binary>(array[static_cast<int32_t>(index)]);
    }
    case BinJSONType::NUMBER: {
        if (!hasBytes(offset, sizeof(double), limit)) {
            return parseError("Unexpected end of binJSON data");
        }
        double value;
        std::memcpy(&value, data + offset, sizeof(value));
        offset += sizeof(value);
        return std::make_shared<Number>(value);
    }
    case BinJSONType::STRING: {
        uint32_t encodedLength;
        if (!readUint32(data, offset, limit, encodedLength)
            || encodedLength > static_cast<uint32_t>(INT32_MAX)) {
            return parseError("Invalid binJSON string length");
        }
        int32_t length = static_cast<int32_t>(encodedLength);
        if (!hasBytes(offset, length, limit)) {
            return parseError("Unexpected end of binJSON data");
        }
        auto value = std::make_shared<String>(
            reinterpret_cast<const char*>(data + offset), length);
        offset += length;
        return value;
    }
    case BinJSONType::ARRAY:
    case BinJSONType::OBJECT: {
        uint32_t encodedDataSize;
        uint32_t count;
        if (!readUint32(data, offset, limit, encodedDataSize)
            || !readUint32(data, offset, limit, count)) {
            return parseError("Unexpected end of binJSON data");
        }
        if (encodedDataSize < sizeof(uint32_t)
            || encodedDataSize > static_cast<uint32_t>(INT32_MAX)
            || !hasBytes(offset, static_cast<int32_t>(encodedDataSize)
                - static_cast<int32_t>(sizeof(uint32_t)), limit)) {
            return parseError("Invalid binJSON container size");
        }
        int32_t containerEnd = offset + static_cast<int32_t>(encodedDataSize)
            - static_cast<int32_t>(sizeof(uint32_t));

        if (type == BinJSONType::ARRAY) {
            if (count > static_cast<uint32_t>(containerEnd - offset)) {
                return parseError("Invalid binJSON array element count");
            }
            auto result = std::make_shared<Array>();
            result->value.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                auto element = parseValue(data, offset, containerEnd,
                    array, arrayOffset, bufferIndex, depth + 1);
                if (element->isError()) {
                    return element;
                }
                result->value.emplace_back(std::move(element));
            }
            if (offset != containerEnd) {
                return parseError("BinJSON array size does not match its contents");
            }
            return result;
        }

        if (count > static_cast<uint32_t>((containerEnd - offset)
            / (static_cast<int32_t>(sizeof(uint32_t)) + 1))) {
            return parseError("Invalid binJSON object property count");
        }
        auto result = std::make_shared<Object>();
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t encodedNameLength;
            if (!readUint32(data, offset, containerEnd, encodedNameLength)
                || encodedNameLength > static_cast<uint32_t>(INT32_MAX)) {
                return parseError("Invalid binJSON object property name length");
            }
            int32_t nameLength = static_cast<int32_t>(encodedNameLength);
            if (!hasBytes(offset, nameLength, containerEnd)) {
                return parseError("Unexpected end of binJSON data");
            }
            std::string name(reinterpret_cast<const char*>(data + offset), nameLength);
            offset += nameLength;
            auto property = parseValue(data, offset, containerEnd, array,
                arrayOffset, bufferIndex, depth + 1);
            if (property->isError()) {
                return property;
            }
            result->value.emplace(std::move(name), std::move(property));
        }
        if (offset != containerEnd) {
            return parseError("BinJSON object size does not match its contents");
        }
        return result;
    }
    default:
        return parseError("Unknown binJSON type");
    }
}

}

std::shared_ptr<Value> parse(Buffer<> buffer, int32_t bufferOffset,
    const std::vector<Buffer<>>& array, int32_t arrayOffset, int32_t bufferIndex)
{
    if (bufferOffset < 0) {
        return std::make_shared<Error>("Invalid binJSON buffer offset");
    }
    if (arrayOffset < 0 || bufferIndex < 0) {
        return std::make_shared<Error>("Invalid binJSON buffer index offset");
    }
    if (bufferOffset > buffer.size()) {
        return std::make_shared<Error>("BinJSON buffer offset is out of range");
    }

    int32_t offset = bufferOffset;
    return parseValue(buffer.raw<const uint8_t*>(), offset, buffer.size(),
        array, arrayOffset, bufferIndex, 0);
}

template<bool grow = true, class T>
static inline int32_t write(Buffer<>& buffer, int32_t offset, T* data, int32_t size)
{
    if (grow) {
        buffer.grow(offset + size);
    }
    std::memcpy(buffer.raw<uint8_t*>() + offset, data, size);
    return offset + size;
}


template<bool grow = true, class T>
static inline int32_t write(Buffer<>& buffer, int32_t offset, T& value)
{
    return write<grow>(buffer, offset, &value, sizeof(T));
}

static void generateRecursive(const std::shared_ptr<Value>& value, Buffer<>& buffer,
    std::vector<Buffer<>>& array, int32_t bufferIndexOffset)
{
    struct {
        uint32_t dataSize;
        uint32_t count;
    } containerHeader{};

    int32_t offset = buffer.size();
    buffer.grow(offset + 1);
    auto type = value->getType();
    if (type == BinJSONType::ERROR) {
        type = BinJSONType::NUL;
    }
    buffer.raw<uint8_t*>()[offset] = (uint8_t)(type);
    offset++;

    switch (type)
    {
    case BinJSONType::NUL: {
        break;
    }
    case BinJSONType::BINARY: {
        uint32_t v = array.size() + bufferIndexOffset;
        CUCUT_ASSERT(v < INT32_MAX, "Invalid bufferIndexOffset");
        array.emplace_back(value->buffer());
        write(buffer, offset, v);
        break;
    }
    case BinJSONType::NUMBER: {
        double v = value->number();
        write(buffer, offset, v);
        break;
    }
    case BinJSONType::STRING: {
        auto& v = value->string();
        uint32_t len = v.length();
        offset = write(buffer, offset, len);
        write(buffer, offset, v.data(), len);
        break;
    }
    case BinJSONType::ARRAY: {
        auto v = value->to<Array>();
        write(buffer, offset, containerHeader);
        for (auto element : v->value) {
            generateRecursive(element, buffer, array, bufferIndexOffset);
        }
        int32_t endOffset = buffer.size();
        containerHeader.dataSize = endOffset - offset - sizeof(uint32_t);
        containerHeader.count = v->length();
        write<false>(buffer, offset, containerHeader);
        break;
    }
    case BinJSONType::OBJECT: {
        auto v = value->to<Object>();
        write(buffer, offset, containerHeader);
        for (auto property : v->value) {
            uint32_t len = property.first.length();
            int32_t propertyOffset = buffer.size();
            propertyOffset = write(buffer, propertyOffset, len);
            propertyOffset = write(buffer, propertyOffset, property.first.data(), len);
            generateRecursive(property.second, buffer, array, bufferIndexOffset);
        }
        int32_t endOffset = buffer.size();
        containerHeader.dataSize = endOffset - offset - sizeof(uint32_t);
        containerHeader.count = v->length();
        write<false>(buffer, offset, containerHeader);
        break;
    }
    default:
        CUCUT_ASSERT(false, "Unknow type of value.");
        break;
    }
}


void generate(const std::shared_ptr<Value>& value, Buffer<>& buffer, int32_t bufferOffset,
    std::vector<Buffer<>>& array, int32_t arrayOffset, int32_t bufferIndex)
{
    buffer.grow(bufferOffset);
    array.resize(arrayOffset);
    generateRecursive(value, buffer, array, bufferIndex - arrayOffset);
}

};

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
    "CuCut is only compatible with little-endian machines.");
#elif defined(_WIN32)
static_assert(true, "CuCut is only compatible with little-endian machines.");
#else
#error "Cannot verify target byte order; CuCut is only compatible with little-endian machines."
#endif
