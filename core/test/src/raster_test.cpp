#include "includes.hpp"

namespace raster_test {

#include "fatal_error_to_exception.hpp"
#include "raster.hpp"

TEST(RasterTest, CreateInitializesMetadataAndAllocatesAlignedRows)
{
    auto raster = Raster<int16_t>::create(3, 2);

    EXPECT_EQ(raster->width, 3);
    EXPECT_EQ(raster->height, 2);
    EXPECT_EQ(raster->stride, 8);
    EXPECT_EQ(raster->pixelBytes, (int)sizeof(int16_t));
    EXPECT_EQ(raster.size(), static_cast<int32_t>(offsetof(Raster<int16_t>, data) + 16));
}

TEST(RasterTest, CreateWithFillValueInitializesEveryPixel)
{
    auto raster = Raster<uint8_t>::create(3, 2, 0xA5);

    for (int32_t y = 0; y < raster->height; ++y) {
        for (int32_t x = 0; x < raster->width; ++x) {
            EXPECT_EQ(raster->data[y * raster->stride + x], 0xA5);
        }
    }
}

TEST(RasterTest, FillCopiesNonRepeatingValuesPerPixelUsingStride)
{
    auto raster = Raster<PixelLocation>::create(1, 2);
    auto* bytes = reinterpret_cast<uint8_t*>(raster->data);
    for (int32_t y = 0; y < raster->height; ++y) {
        std::memset(bytes + y * raster->stride, 0xCC, raster->stride);
    }

    const PixelLocation value{0x1234, 0x5678};
    raster->fill(value);

    for (int32_t y = 0; y < raster->height; ++y) {
        auto* row = reinterpret_cast<PixelLocation*>(bytes + y * raster->stride);
        EXPECT_EQ(row[0].x, value.x);
        EXPECT_EQ(row[0].y, value.y);
        EXPECT_EQ(bytes[y * raster->stride + sizeof(PixelLocation)], 0xCC);
    }
}

TEST(RasterTest, CloneCreatesIndependentExactCopy)
{
    auto raster = Raster<int16_t>::create(3, 2, static_cast<int16_t>(-12));
    Buffer<Raster<int16_t>> shared(raster);

    auto clone = raster->clone();

    EXPECT_NE(clone.raw<uint8_t*>(), raster.raw<uint8_t*>());
    EXPECT_EQ(clone.size(), raster.size());
    EXPECT_EQ(clone->width, raster->width);
    EXPECT_EQ(clone->height, raster->height);
    EXPECT_EQ(clone->stride, raster->stride);
    EXPECT_EQ(clone->pixelBytes, raster->pixelBytes);
    EXPECT_EQ(clone->data[clone->stride / sizeof(int16_t) + 2], -12);

    clone->data[0] = 7;
    EXPECT_EQ(raster->data[0], -12);
    EXPECT_EQ(shared->data[0], -12);
}

}
