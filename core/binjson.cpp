
#include <cstdint>

#include "binjson.hpp"

namespace BinJSON {

namespace {

static constexpr int32_t maximumParseDepth = 256;

static Any parseError(const char* message)
{
    return makeError(message);
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

static Any parseValue(const uint8_t* data, int32_t& offset,
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
        return makeNull();
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
        return makeBinary(array[static_cast<int32_t>(index)]);
    }
    case BinJSONType::BOOLEAN: {
        uint32_t value;
        if (!readUint32(data, offset, limit, value)) {
            return parseError("Unexpected end of binJSON data");
        }
        return makeBoolean(value != 0);
    }
    case BinJSONType::NUMBER: {
        if (!hasBytes(offset, sizeof(double), limit)) {
            return parseError("Unexpected end of binJSON data");
        }
        double value;
        std::memcpy(&value, data + offset, sizeof(value));
        offset += sizeof(value);
        return makeNumber(value);
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
        auto value = makeString(
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
            auto result = makeArray();
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
        auto result = makeObject();
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

Any parse(Buffer<> buffer, int32_t bufferOffset,
    const std::vector<Buffer<>>& array, int32_t arrayOffset, int32_t bufferIndex)
{
    if (bufferOffset < 0) {
        return makeError("Invalid binJSON buffer offset");
    }
    if (arrayOffset < 0 || bufferIndex < 0) {
        return makeError("Invalid binJSON buffer index offset");
    }
    if (bufferOffset > buffer.size()) {
        return makeError("BinJSON buffer offset is out of range");
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

static void generateRecursive(const Any& value, Buffer<>& buffer,
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
    case BinJSONType::BOOLEAN: {
        uint32_t v = value->boolean() ? 1 : 0;
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
        auto v = value->to<ArrayValue>();
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
        auto v = value->to<ObjectValue>();
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


void generate(const Any& value, Buffer<>& buffer, int32_t bufferOffset,
    std::vector<Buffer<>>& array, int32_t arrayOffset, int32_t bufferIndex)
{
    buffer.grow(bufferOffset);
    array.resize(arrayOffset);
    generateRecursive(value, buffer, array, bufferIndex - arrayOffset);
}

static void writeString(Buffer<char>& buffer, const char* text)
{
    int32_t offset = buffer.size() - 1;
    int totalSize = strlen(text) + 1;
    buffer.grow(offset + totalSize);
    std::memcpy(buffer.raw() + offset, text, totalSize);
}


static void writeEscapedString(Buffer<char>& buffer, const std::string& text)
{
    int32_t length = text.length();
    auto dst = buffer.raw() + buffer.size() - 1;
    auto dstResize = dst + length + 1;
    buffer.grow(buffer.size() + length + 10);
    auto src = text.c_str();
    auto srcEnd = src + length;
    while (src < srcEnd) {
        if (dst >= dstResize) {
            auto dst = buffer.raw() + buffer.size() - 1;
            auto dstResize = dst + length + 1;
            buffer.grow(buffer.size() + length + 10);            
        }
        char c = *src++;
        switch (c) {
        case '"':
            dst[0] = '\\';
            dst[1] = '"';
            dst += 2;
            break;
        case '\\':
            dst[0] = '\\';
            dst[1] = '\\';
            dst += 2;
            break;
        case '\b':
            dst[0] = '\\';
            dst[1] = 'b';
            dst += 2;
            break;
        case '\f':
            dst[0] = '\\';
            dst[1] = 'f';
            dst += 2;
            break;
        case '\n':
            dst[0] = '\\';
            dst[1] = 'n';
            dst += 2;
            break;
        case '\r':
            dst[0] = '\\';
            dst[1] = 'r';
            dst += 2;
            break;
        case '\t':
            dst[0] = '\\';
            dst[1] = 't';
            dst += 2;
            break;
        case '\x7F':
            dst[0] = '\\';
            dst[1] = 'u';
            dst[2] = '0';
            dst[3] = '0';
            dst[4] = '7';
            dst[5] = 'F';
            dst += 6;
            break;
        default:
            if ((uint8_t)c >= ' ') {
                *dst++ = c;
            } else {
                static const char hex[] = "0123456789ABCDEF";
                dst[0] = '\\';
                dst[1] = 'u';
                dst[2] = '0';
                dst[3] = '0';
                dst[4] = hex[(c >> 4) & 0x03];
                dst[5] = hex[c & 0x0F];
                dst += 6;
            }
            break;
        }
    }
}

static void writeString(Buffer<char>& buffer, const std::string& text)
{
    int32_t offset = buffer.size() - 1;
    int totalSize = text.length() + 1;
    buffer.grow(offset + totalSize);
    std::memcpy(buffer.raw() + offset, text.c_str(), totalSize);
}

static void writeInt(Buffer<char>& buffer, int value)
{
    int32_t offset = buffer.size() - 1;
    buffer.grow(offset + 16);
    int num = sprintf(buffer.raw() + offset, "%d", value);
    buffer.grow(offset + num + 1);
}

static void writeDouble(Buffer<char>& buffer, double value)
{
    int32_t offset = buffer.size() - 1;
    buffer.grow(offset + 16 + DBL_DECIMAL_DIG);
    int num = snprintf(buffer.raw() + offset, 16 + DBL_DECIMAL_DIG, "%.*g", DBL_DECIMAL_DIG, value);
    buffer.grow(offset + num + 1);
}

static void writeIndent(Buffer<char>& buffer, int indent)
{
    int32_t offset = buffer.size() - 1;
    buffer.grow(offset + indent + 1);
    std::memset(buffer.raw() + offset, ' ', indent);
    buffer.raw()[offset + indent] = 0;
}

static void stringifyRecursive(const Any& value, Buffer<char>& buffer,
    std::vector<Buffer<>>& array, int32_t bufferIndexOffset, int indent)
{
    auto type = value->getType();
    int32_t offset = buffer.size() - 1;

    switch (type)
    {
    case BinJSONType::NUL: {
        writeString(buffer, "null");
        break;
    }
    case BinJSONType::ERROR: {
        writeString(buffer, (indent > 0) ? "{ \"\\u001A\": \"error\", \"message\": " : "{\"\\u001A\":\"error\",\"message\":");
        writeEscapedString(buffer, value->string());
        writeString(buffer, (indent > 0) ? " }" : "}");
        break;
    }
    case BinJSONType::BINARY: {
        uint32_t v = array.size() + bufferIndexOffset;
        array.emplace_back(value->buffer());
        writeString(buffer, (indent > 0) ? "{ \"\\u001A\": \"binary\", \"index\": " : "{\"\\u001A\":\"binary\",\"index\":");
        writeInt(buffer, v);
        writeString(buffer, (indent > 0) ? " }" : "}");
        break;
    }
    case BinJSONType::BOOLEAN: {
        writeString(buffer, value->boolean() ? "true" : "false");
        break;
    }
    case BinJSONType::NUMBER: {
        writeDouble(buffer, value->number());
        break;
    }
    case BinJSONType::STRING: {
        writeString(buffer, "\"");
        writeEscapedString(buffer, value->string());
        writeString(buffer, "\"");
        break;
    }
    case BinJSONType::ARRAY: {
        auto v = value->to<ArrayValue>();
        int count = v->length();
        if (count > 0) {
            writeString(buffer, (indent > 0) ? "[" : "[\n");
            for (int i = 0; i < count; i++) {
                writeIndent(buffer, indent + 1);
                stringifyRecursive(v->get(i), buffer, array, bufferIndexOffset, indent + 1);
                if (i < count - 1) {
                    writeString(buffer, (indent > 0) ? "," : ",\n");
                } else {
                    writeString(buffer, (indent > 0) ? "" : "\n");
                }
            }
            writeIndent(buffer, indent);
            writeString(buffer, "]");
        } else {
            writeString(buffer, "[]");
        }
        break;
    }
    case BinJSONType::OBJECT: {
        auto v = value->to<ObjectValue>();
        int count = v->length();
        if (count > 0) {
            writeString(buffer, (indent > 0) ? "{" : "{\n");
            int i = 0;
            for (auto pair : v->value) {
                writeIndent(buffer, indent + 1);
                writeEscapedString(buffer, pair.first);
                writeString(buffer, (indent > 0) ? ": " : ":");
                stringifyRecursive(v->get(i), buffer, array, bufferIndexOffset, indent + 1);
                if (i < count - 1) {
                    writeString(buffer, (indent > 0) ? "," : ",\n");
                } else {
                    writeString(buffer, (indent > 0) ? "" : "\n");
                }
                i++;
            }
            writeIndent(buffer, indent);
            writeString(buffer, "]");
        } else {
            writeString(buffer, "{}");
        }
        break;
    }
    default:
        CUCUT_ASSERT(false, "Unknow type of value.");
        break;
    }
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
