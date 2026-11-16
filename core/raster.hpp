#ifndef CUCUT_IMAGE_HPP
#define CUCUT_IMAGE_HPP


#include "buffer.hpp"


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
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t pixelBytes;
    Aux aux;
    T data[sizeof(Aux) == RawBuffer::alignment - 16 ? 1 : -1];

    static constexpr strideAlign = 8;
    static constexpr expectedPixelBytes = sizeof(T);
    static constexpr calcStride(int32_t width) {
        return (width * expectedPixelBytes + strideAlign - 1) / strideAlign;
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

struct PixelRectangle
{
    /** Inclusive */
    PixelLocation begin;
    /** Exclusive */
    PixelLocation end;
};

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
