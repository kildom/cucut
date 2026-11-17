#include "includes.hpp"

namespace edt_test {

#include "../../commands.cpp"
#include "../../commands/edt.cpp"

template<class T>
static Buffer<Raster<float>> runEdt(
    const Buffer<T>& bitmap,
    const char* objectCondition,
    double objectValue,
    BinJSON::Any bbox = BinJSON::makeNull())
{
    auto command = binJSONObject({
        { "command", binJSON("edt") },
        { "bitmap", binJSON(bitmap) },
        { "objectCondition", binJSON(objectCondition) },
        { "objectValue", binJSON(objectValue) },
        { "bbox", bbox },
    });
    const auto response = edt(command);
    EXPECT_FALSE(response->isError()) << response->error();
    return response->get("distance")->template buffer<Raster<float>>();
}

static float distanceAt(const Buffer<Raster<float>>& raster, int32_t x, int32_t y)
{
    const auto* row = reinterpret_cast<const float*>(
        reinterpret_cast<const uint8_t*>(raster->data) + y * raster->stride);
    return row[x];
}

TEST(EdtTest, SignedConditionReturnsEuclideanDistanceToNearestObject)
{
    auto bitmap = Raster<int16_t>::create(5, 5, static_cast<int16_t>(-1));
    bitmap->data[2 * bitmap->stride / static_cast<int32_t>(sizeof(int16_t)) + 2] = 0;
    bitmap->data[2 * bitmap->stride / static_cast<int32_t>(sizeof(int16_t)) + 3] = 1;

    const auto result = runEdt(bitmap, "s>", -1);

    ASSERT_EQ(result->width, 5);
    ASSERT_EQ(result->height, 5);
    EXPECT_FLOAT_EQ(distanceAt(result, 2, 2), 0.0f);
    EXPECT_FLOAT_EQ(distanceAt(result, 3, 2), 0.0f);
    EXPECT_FLOAT_EQ(distanceAt(result, 1, 2), 1.0f);
    EXPECT_FLOAT_EQ(distanceAt(result, 2, 1), 1.0f);
    EXPECT_FLOAT_EQ(distanceAt(result, 1, 1), std::sqrt(2.0f));
    EXPECT_FLOAT_EQ(distanceAt(result, 0, 0), std::sqrt(8.0f));
}

TEST(EdtTest, BoundingBoxLimitsInputObjectsAndOutputDimensions)
{
    auto bitmap = Raster<uint8_t>::create(5, 4, static_cast<uint8_t>(0));
    bitmap->data[1 * bitmap->stride + 1] = 1;
    bitmap->data[3 * bitmap->stride + 4] = 1;
    auto bbox = binJSONObject({
        { "begin", binJSONObject({ { "x", binJSON(1) }, { "y", binJSON(1) } }) },
        { "end", binJSONObject({ { "x", binJSON(4) }, { "y", binJSON(4) } }) },
    });

    const auto result = runEdt(bitmap, "u>", 0, bbox);

    ASSERT_EQ(result->width, 3);
    ASSERT_EQ(result->height, 3);
    EXPECT_FLOAT_EQ(distanceAt(result, 0, 0), 0.0f);
    EXPECT_FLOAT_EQ(distanceAt(result, 2, 2), std::sqrt(8.0f));
}

TEST(EdtTest, EmptyObjectSetProducesInfiniteDistances)
{
    auto bitmap = Raster<uint32_t>::create(3, 2, 0U);

    const auto result = runEdt(bitmap, "u>", 0);

    for (int32_t y = 0; y < result->height; ++y) {
        for (int32_t x = 0; x < result->width; ++x) {
            EXPECT_TRUE(std::isinf(distanceAt(result, x, y)));
        }
    }
}

TEST(EdtTest, SupportsEightBytePixelsAndHighObjectValueWord)
{
    auto bitmap = Raster<uint64_t>::create(2, 1, 0ULL);
    bitmap->data[1] = (static_cast<uint64_t>(3) << 32) | 7;
    auto command = binJSONObject({
        { "bitmap", binJSON(bitmap) },
        { "objectCondition", binJSON("==") },
        { "objectValue", binJSON(7) },
        { "objectValueHi32", binJSON(3) },
    });

    const auto response = edt(command);
    ASSERT_FALSE(response->isError()) << response->error();
    const auto result = response->get("distance")->buffer<Raster<float>>();
    EXPECT_FLOAT_EQ(distanceAt(result, 0, 0), 1.0f);
    EXPECT_FLOAT_EQ(distanceAt(result, 1, 0), 0.0f);
}

}
