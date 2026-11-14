

#include "binjson.hpp"

namespace BinJSON {

template<bool grow = true, class T>
static inline int32_t write(Buffer& buffer, int32_t offset, T* data, int32_t size)
{
    if (grow) {
        buffer.grow(offset + sizeof(T));
    }
    std::memcpy(buffer.to<uint8_t*>() + offset, &value, sizeof(T));
    return offset + sizeof(T);
}


template<bool grow = true, class T>
static inline int32_t write(Buffer& buffer, int32_t offset, T& value)
{
    return write<grow>(buffer, offset, &value, sizeof(T));
}

static void generateRecursive(const std::shared_ptr<Value>& value, Buffer& buffer,
    std::vector<Buffer>& array, int32_t bufferIndexOffset)
{
    struct {
        uint32_t dataSize;
        uint32_t count;
    } containerHeader;

    int32_t offset = buffer.size();
    buffer.grow(offset + 1);
    buffer.to<uint8_t*>()[offset] = (uint8_t)value->getType();
    offset++;

    switch (value->getType())
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


void generate(const std::shared_ptr<Value>& value, Buffer& buffer, int32_t bufferOffset,
    std::vector<Buffer>& array, int32_t arrayOffset, int32_t bufferIndex)
{
    buffer.grow(bufferOffset);
    array.resize(arrayOffset);
    generateRecursive(value, buffer, array, bufferIndex - arrayOffset);
}

}
