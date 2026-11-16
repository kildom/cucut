#ifndef CUCUT_IMAGE_HPP
#define CUCUT_IMAGE_HPP


#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "buffer.hpp"
#include "binjson.hpp"


/** @brief Generic 2D raster data.
 * 
 * Data starts with top-left corner and goes horizontally first.
 * Each row of the raster is `stride` bytes apart.
 * Each pixel of a raster is `pixelBytes` bytes long.
 * Auxilary data can be at the `aux` field.
 */
template<class T, class Aux = uint8_t[RawBuffer::alignment - 16]>
struct Raster
{
    static_assert(std::is_trivially_copyable_v<T>, "Raster pixels must be trivially copyable");
    static_assert(alignof(T) <= 8, "Raster pixel alignment cannot exceed the stride alignment");

    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t pixelBytes;
    Aux aux;
    T data[sizeof(Aux) == RawBuffer::alignment - 16 ? 1 : -1];

    static constexpr int32_t strideAlign = 8;
    static constexpr int32_t expectedPixelBytes = sizeof(T);

    static constexpr int32_t calcStride(int32_t width)
    {
        return ((width * expectedPixelBytes + strideAlign - 1) / strideAlign) * strideAlign;
    }

    /** @brief Allocate a raster with aligned rows and initialized metadata. */
    static Buffer<Raster> create(int32_t width, int32_t height)
    {
        if (height < 0) {
            fatalError("Raster height cannot be negative.");
        }
        const int32_t stride = calcStride(width);
        const int64_t dataSize = static_cast<int64_t>(stride) * height;
        const int64_t allocationSize =
            static_cast<int64_t>(offsetof(Raster, data)) + dataSize;
        if (allocationSize > RawBuffer::maximumCapacity) {
            fatalError("Raster is too large.");
        }

        Buffer<Raster> raster(static_cast<int32_t>(
            allocationSize < static_cast<int64_t>(sizeof(Raster)) ? sizeof(Raster) : allocationSize));
        raster->width = width;
        raster->height = height;
        raster->stride = stride;
        raster->pixelBytes = expectedPixelBytes;
        return raster;
    }

    /** @brief Allocate and initialize every raster pixel to the specified value. */
    static Buffer<Raster> create(int32_t width, int32_t height, const T& fillValue)
    {
        Buffer<Raster> raster = create(width, height);
        raster->fill(fillValue);
        return raster;
    }

    /** @brief Fill all pixels, respecting the byte stride between rows. */
    void fill(const T& value)
    {
        const auto* valueBytes = reinterpret_cast<const uint8_t*>(&value);
        bool hasRepeatingByte = true;
        for (int32_t i = 1; i < expectedPixelBytes; ++i) {
            if (valueBytes[i] != valueBytes[0]) {
                hasRepeatingByte = false;
                break;
            }
        }

        for (int32_t y = 0; y < height; ++y) {
            auto* row = reinterpret_cast<uint8_t*>(data) + y * stride;
            if (hasRepeatingByte) {
                std::memset(row, valueBytes[0], width * expectedPixelBytes);
            } else {
                for (int32_t x = 0; x < width; ++x) {
                    std::memcpy(row + x * expectedPixelBytes, &value, expectedPixelBytes);
                }
            }
        }
    }

    /** @brief Return an independent buffer containing an exact copy of this raster. */
    Buffer<Raster> clone() const
    {
        Buffer<Raster> result(const_cast<Raster*>(this));
        result.unique();
        return result;
    }
};

/** @brief Structure containing pixel location.
 * 
 * Negative value can be reserved for special uses and it depends on context.
 */
struct PixelLocation
{
    int16_t x;
    int16_t y;
};

[[maybe_unused]]
static BinJSON::Object binJSON(const PixelLocation& value) {
    return binJSONObject({
        { "x", binJSON(value.x) },
        { "y", binJSON(value.y) },
    });
}

struct PixelRectangle
{
    /** Inclusive */
    PixelLocation begin;
    /** Exclusive */
    PixelLocation end;
};

[[maybe_unused]]
static BinJSON::Object binJSON(const PixelRectangle& value) {
    return binJSONObject({
        { "begin", binJSON(value.begin) },
        { "end", binJSON(value.end) },
    });
}

/** @brief Raster containing a map (on/off) pixels
 */
using PixelMapRaster = Raster<uint8_t>;

/** @brief Raster containing labels for each pixel
 */
using LabelRaster = Raster<int16_t>;

/** @brief Raster containing a queue of pixels
 * 
 * Each pixel can point to a next pixel, so it creates a queue
 * or a list of pixels.
 */
using PixelQueueRaster = Raster<PixelLocation>;


#endif
