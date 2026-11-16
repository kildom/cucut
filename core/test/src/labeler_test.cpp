#include "includes.hpp"

namespace labeler_test {

#include "../../commands.cpp"
#include "../../flood.cpp"
#include "../../commands/labeler.cpp"

struct LabelerResult
{
    Buffer<LabelRaster> raster;
    BinJSON::Array info;
};

static LabelerResult runLabeler(const std::vector<std::string>& rows, bool is8Neighborhood)
{
    const int32_t width = static_cast<int32_t>(rows[0].size());
    const int32_t height = static_cast<int32_t>(rows.size());
    auto source = PixelMapRaster::create(width, height, static_cast<uint8_t>(0));
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            source->data[y * source->stride + x] = rows[y][x] == '1' ? 1 : 0;
        }
    }

    auto info = BinJSON::makeArray();
    auto raster = labeler(std::move(source), info, is8Neighborhood);
    return { std::move(raster), std::move(info) };
}

static int16_t pixel(const Buffer<LabelRaster>& raster, int32_t x, int32_t y)
{
    const auto* row = reinterpret_cast<const int16_t*>(
        reinterpret_cast<const uint8_t*>(raster->data) + y * raster->stride);
    return row[x];
}

static int32_t number(const BinJSON::Any& value, const char* property)
{
    return static_cast<int32_t>(value->get(property)->number());
}

static void expectPoint(const BinJSON::Any& point, int32_t x, int32_t y)
{
    EXPECT_EQ(number(point, "x"), x);
    EXPECT_EQ(number(point, "y"), y);
}

TEST(LabelerTest, FourNeighborhoodSeparatesDiagonalComponentsAndReportsMetadata)
{
    const auto result = runLabeler({
        "11000",
        "01010",
        "00010",
        "00001",
    }, false);

    ASSERT_EQ(result.info->length(), 3);
    EXPECT_EQ(pixel(result.raster, 0, 0), 0);
    EXPECT_EQ(pixel(result.raster, 1, 1), 0);
    EXPECT_EQ(pixel(result.raster, 3, 1), 1);
    EXPECT_EQ(pixel(result.raster, 3, 2), 1);
    EXPECT_EQ(pixel(result.raster, 4, 3), 2);
    EXPECT_EQ(pixel(result.raster, 2, 0), -1);

    const auto& first = result.info->get(0);
    EXPECT_EQ(number(first, "index"), 0);
    EXPECT_EQ(number(first, "pixelCount"), 3);
    expectPoint(first->get("topLeft"), 0, 0);
    expectPoint(first->get("bbox")->get("begin"), 0, 0);
    expectPoint(first->get("bbox")->get("end"), 2, 2);

    const auto& second = result.info->get(1);
    EXPECT_EQ(number(second, "pixelCount"), 2);
    expectPoint(second->get("topLeft"), 3, 1);
    expectPoint(second->get("bbox")->get("begin"), 3, 1);
    expectPoint(second->get("bbox")->get("end"), 4, 3);

    const auto& third = result.info->get(2);
    EXPECT_EQ(number(third, "pixelCount"), 1);
    expectPoint(third->get("topLeft"), 4, 3);
    expectPoint(third->get("bbox")->get("begin"), 4, 3);
    expectPoint(third->get("bbox")->get("end"), 5, 4);
}

TEST(LabelerTest, EightNeighborhoodConnectsDiagonalPixels)
{
    const auto result = runLabeler({
        "100",
        "010",
        "001",
    }, true);

    ASSERT_EQ(result.info->length(), 1);
    EXPECT_EQ(pixel(result.raster, 0, 0), 0);
    EXPECT_EQ(pixel(result.raster, 1, 1), 0);
    EXPECT_EQ(pixel(result.raster, 2, 2), 0);
    EXPECT_EQ(pixel(result.raster, 1, 0), -1);

    const auto& info = result.info->get(0);
    EXPECT_EQ(number(info, "index"), 0);
    EXPECT_EQ(number(info, "pixelCount"), 3);
    expectPoint(info->get("topLeft"), 0, 0);
    expectPoint(info->get("bbox")->get("begin"), 0, 0);
    expectPoint(info->get("bbox")->get("end"), 3, 3);
}

TEST(LabelerTest, EmptyBitmapProducesOnlyBackgroundPixels)
{
    const auto result = runLabeler({
        "000",
        "000",
    }, false);

    EXPECT_EQ(result.info->length(), 0);
    for (int32_t y = 0; y < result.raster->height; ++y) {
        for (int32_t x = 0; x < result.raster->width; ++x) {
            EXPECT_EQ(pixel(result.raster, x, y), -1);
        }
    }
}

}
