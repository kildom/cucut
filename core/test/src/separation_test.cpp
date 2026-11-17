#include "includes.hpp"

namespace separation_test {

#include "../../commands.cpp"
#include "../../commands/separation.cpp"

static Buffer<Raster<float>> distanceField(int32_t width, int32_t height, const std::vector<Point>& objects)
{
    auto raster = Raster<float>::create(width, height);
    for (int32_t y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(raster->data) + y * raster->stride);
        for (int32_t x = 0; x < width; ++x) {
            float minimum = std::numeric_limits<float>::infinity();
            for (const Point& object : objects) {
                const float dx = static_cast<float>(x - object.x);
                const float dy = static_cast<float>(y - object.y);
                minimum = std::min(minimum, std::sqrt(dx * dx + dy * dy));
            }
            row[x] = minimum;
        }
    }
    return raster;
}

static BinJSON::Any runSeparation(
    const Buffer<Raster<float>>& distances,
    int32_t radius,
    const char* direction,
    const char* edge,
    int32_t startX,
    int32_t startY)
{
    const auto command = binJSONObject({
        { "command", binJSON("separation") },
        { "distance", binJSON(distances) },
        { "radius", binJSON(radius) },
        { "direction", binJSON(direction) },
        { "edge", binJSON(edge) },
        { "startingPoint", binJSONObject({
            { "x", binJSON(startX) },
            { "y", binJSON(startY) },
        }) },
    });
    return executeCommand(command);
}

static std::vector<PathPoint> pathPoints(const BinJSON::Any& response)
{
    const auto bytes = response->get("path")->buffer<float>();
    EXPECT_EQ(bytes.size() % (2 * static_cast<int32_t>(sizeof(float))), 0);
    const int32_t count = bytes.size() / (2 * static_cast<int32_t>(sizeof(float)));
    const float* values = bytes.raw<float*>();
    std::vector<PathPoint> result;
    for (int32_t index = 0; index < count; ++index) {
        result.push_back({ values[index * 2], values[index * 2 + 1] });
    }
    return result;
}

TEST(SeparationTest, TracesClosedOuterPathInRequestedDirection)
{
    std::vector<Point> objectPixels;
    for (int32_t y = 10; y <= 12; ++y) {
        for (int32_t x = 10; x <= 12; ++x) {
            objectPixels.push_back({ x, y });
        }
    }
    const auto distances = distanceField(32, 32, objectPixels);

    const auto ccwResponse = runSeparation(distances, 4, "ccw", "outer", 10, 10);
    ASSERT_FALSE(ccwResponse->isError()) << ccwResponse->error();
    const auto cwResponse = runSeparation(distances, 4, "cw", "outer", 10, 10);
    ASSERT_FALSE(cwResponse->isError()) << cwResponse->error();
    const auto ccw = pathPoints(ccwResponse);
    const auto cw = pathPoints(cwResponse);

    ASSERT_GT(ccw.size(), 8u);
    ASSERT_EQ(ccw.size(), cw.size());
    EXPECT_FLOAT_EQ(ccw.front().x, cw.front().x);
    EXPECT_FLOAT_EQ(ccw.front().y, cw.front().y);
    EXPECT_TRUE(ccw[1].x != cw[1].x || ccw[1].y != cw[1].y);
    for (const PathPoint& point : ccw) {
        EXPECT_TRUE(std::isfinite(point.x));
        EXPECT_TRUE(std::isfinite(point.y));
        EXPECT_GE(point.x, 0.0f);
        EXPECT_LT(point.x, distances->width);
        EXPECT_GE(point.y, 0.0f);
        EXPECT_LT(point.y, distances->height);
    }
}

TEST(SeparationTest, FindsInnerPathFromHoleStartingPoint)
{
    std::vector<Point> ringPixels;
    for (int32_t coordinate = 5; coordinate <= 21; ++coordinate) {
        ringPixels.push_back({ coordinate, 5 });
        ringPixels.push_back({ coordinate, 21 });
        ringPixels.push_back({ 5, coordinate });
        ringPixels.push_back({ 21, coordinate });
    }
    const auto distances = distanceField(32, 32, ringPixels);

    const auto response = runSeparation(distances, 6, "ccw", "inner", 12, 7);

    ASSERT_FALSE(response->isError()) << response->error();
    EXPECT_GT(pathPoints(response).size(), 8u);
}

TEST(SeparationTest, ReportsInvalidStartingPointAndRasterEdge)
{
    const auto distances = distanceField(8, 8, { { 2, 2 } });
    const auto invalidStart = runSeparation(distances, 2, "ccw", "outer", 3, 2);
    EXPECT_TRUE(invalidStart->isError());

    const auto edgeStart = runSeparation(distances, 20, "ccw", "outer", 2, 2);
    ASSERT_TRUE(edgeStart->isError());
    EXPECT_NE(edgeStart->error().find("raster edge"), std::string::npos);
}

TEST(SeparationTest, RejectsInvalidRequestFields)
{
    const auto distances = distanceField(8, 8, { { 4, 4 } });
    const auto invalidRadius = runSeparation(distances, 0, "ccw", "outer", 4, 4);
    EXPECT_TRUE(invalidRadius->isError());

    const auto invalidDirection = runSeparation(distances, 2, "left", "outer", 4, 4);
    EXPECT_TRUE(invalidDirection->isError());
}

}
