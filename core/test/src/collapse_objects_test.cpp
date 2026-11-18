#include "includes.hpp"

namespace collapse_objects_test {

#include "../../commands.cpp"
#include "../../commands/collapseObjects.cpp"

static Buffer<LabelRaster> makeRaster(const std::vector<int16_t>& values)
{
    auto raster = LabelRaster::create(static_cast<int32_t>(values.size()), 1);
    for (int32_t x = 0; x < static_cast<int32_t>(values.size()); ++x) {
        auto* row = reinterpret_cast<int16_t*>(
            reinterpret_cast<uint8_t*>(raster->data));
        row[x] = values[x];
    }
    return raster;
}

static BinJSON::Array makeInfo(int32_t count)
{
    auto info = BinJSON::makeArray();
    for (int32_t index = 0; index < count; ++index) {
        info->value.emplace_back(binJSONObject({ { "index", binJSON(index) } }));
    }
    return info;
}

static std::vector<int16_t> collapse(const std::vector<int16_t>& values, int32_t labelCount)
{
    auto raster = makeRaster(values);
    const auto request = binJSONObject({
        { "command", binJSON("collapseObjects") },
        { "labeled", binJSON(raster) },
        { "info", makeInfo(labelCount) },
    });
    const auto response = collapseObjects(request);
    EXPECT_FALSE(response->isError()) << response->error();
    if (response->isError()) {
        return {};
    }

    const auto output = response->get("collapsed")->buffer<LabelRaster>();
    std::vector<int16_t> result(values.size());
    for (int32_t x = 0; x < static_cast<int32_t>(values.size()); ++x) {
        const auto* row = reinterpret_cast<const int16_t*>(
            reinterpret_cast<const uint8_t*>(output->data));
        result[x] = row[x];
    }
    return result;
}

TEST(CollapseObjectsTest, PropagatesFromBothSidesAndSplitsAtTheNearestFront)
{
    EXPECT_EQ(collapse({ -1, -1, 0, 0, 0, 0, 0, -2, -2 }, 1),
        (std::vector<int16_t>{ -1, -1, -1, -1, -2, -2, -2, -2, -2 }));
}

TEST(CollapseObjectsTest, CollapsesMultipleComponentsIndependently)
{
    EXPECT_EQ(collapse({ -1, 0, 0, -1, 1, 1, -2 }, 2),
        (std::vector<int16_t>{ -1, -1, -1, -1, -1, -2, -2 }));
}

TEST(CollapseObjectsTest, KeepsExistingNegativeRegionsAndRejectsUnboundedObjects)
{
    EXPECT_EQ(collapse({ -2, 0, -2 }, 1),
        (std::vector<int16_t>{ -2, -2, -2 }));

    auto raster = makeRaster({ 0 });
    const auto request = binJSONObject({
        { "command", binJSON("collapseObjects") },
        { "labeled", binJSON(raster) },
        { "info", makeInfo(1) },
    });
    EXPECT_TRUE(collapseObjects(request)->isError());
}

TEST(CollapseObjectsTest, DoesNotUsePreviouslyCollapsedDiagonalObjectsAsSeeds)
{
    auto raster = LabelRaster::create(3, 3, static_cast<int16_t>(-1));
    reinterpret_cast<int16_t*>(reinterpret_cast<uint8_t*>(raster->data))[0] = -2;
    auto* middleRow = reinterpret_cast<int16_t*>(
        reinterpret_cast<uint8_t*>(raster->data) + raster->stride);
    auto* bottomRow = reinterpret_cast<int16_t*>(
        reinterpret_cast<uint8_t*>(raster->data) + 2 * raster->stride);
    middleRow[1] = 0;
    bottomRow[2] = 1;
    const auto request = binJSONObject({
        { "command", binJSON("collapseObjects") },
        { "labeled", binJSON(raster) },
        { "info", makeInfo(2) },
    });

    const auto response = collapseObjects(request);
    ASSERT_FALSE(response->isError()) << response->error();
    const auto output = response->get("collapsed")->buffer<LabelRaster>();
    middleRow = reinterpret_cast<int16_t*>(
        reinterpret_cast<uint8_t*>(output->data) + output->stride);
    bottomRow = reinterpret_cast<int16_t*>(
        reinterpret_cast<uint8_t*>(output->data) + 2 * output->stride);
    EXPECT_EQ(middleRow[1], -2);
    EXPECT_EQ(bottomRow[2], -1);
}

TEST(CollapseObjectsTest, RejectsMetadataWithMismatchedLabelIndices)
{
    auto raster = makeRaster({ -1, 0, -1 });
    const auto request = binJSONObject({
        { "command", binJSON("collapseObjects") },
        { "labeled", binJSON(raster) },
        { "info", BinJSON::makeArray(std::vector<BinJSON::Any>{
            binJSONObject({ { "index", binJSON(1) } }) }) },
    });
    EXPECT_TRUE(collapseObjects(request)->isError());
}

} // namespace collapse_objects_test
