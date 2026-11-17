#include "includes.hpp"

namespace optimize_path_test {

#include "../../commands.cpp"
#include "../../commands/optimizePath.cpp"

static Buffer<float> makePath(const std::vector<PathPoint>& points)
{
    Buffer<float> result(static_cast<int32_t>(points.size() * 2 * sizeof(float)));
    float* coordinates = result.raw<float*>();
    for (int32_t index = 0; index < static_cast<int32_t>(points.size()); ++index) {
        coordinates[index * 2] = points[index].x;
        coordinates[index * 2 + 1] = points[index].y;
    }
    return result;
}

static BinJSON::Any runOptimizePath(const Buffer<float>& path, double maxError)
{
    return executeCommand(binJSONObject({
        { "command", binJSON("optimizePath") },
        { "path", binJSON(path) },
        { "maxError", binJSON(maxError) },
    }));
}

static std::vector<int32_t> indices(const BinJSON::Any& response)
{
    const Buffer<int32_t> buffer = response->get("indices")->buffer<int32_t>();
    const int32_t count = buffer.size() / static_cast<int32_t>(sizeof(int32_t));
    const int32_t* values = buffer.raw<const int32_t*>();
    return std::vector<int32_t>(values, values + count);
}

static std::vector<PathPoint> points(const BinJSON::Any& response)
{
    const Buffer<float> buffer = response->get("path")->buffer<float>();
    const int32_t count = buffer.size() / (2 * static_cast<int32_t>(sizeof(float)));
    const float* values = buffer.raw<const float*>();
    std::vector<PathPoint> result;
    for (int32_t index = 0; index < count; ++index) {
        result.push_back({ values[index * 2], values[index * 2 + 1] });
    }
    return result;
}

TEST(OptimizePathTest, RemovesNearLinePointsAndReturnsOriginalIndices)
{
    const std::vector<PathPoint> input = {
        { 0.0f, 0.0f }, { 2.0f, 0.1f }, { 4.0f, 0.0f }, { 4.0f, 2.0f },
        { 4.0f, 4.0f }, { 2.0f, 4.0f }, { 0.0f, 4.0f }, { 0.0f, 2.0f },
    };

    const auto response = runOptimizePath(makePath(input), 0.2);

    ASSERT_FALSE(response->isError()) << response->error();
    EXPECT_EQ(indices(response), (std::vector<int32_t>{ 0, 2, 4, 6 }));
    const auto result = points(response);
    ASSERT_EQ(result.size(), 4u);
    EXPECT_FLOAT_EQ(result[0].x, input[0].x);
    EXPECT_FLOAT_EQ(result[1].x, input[2].x);
    EXPECT_FLOAT_EQ(result[2].y, input[4].y);
    EXPECT_FLOAT_EQ(result[3].x, input[6].x);
}

TEST(OptimizePathTest, ZeroToleranceKeepsDeviatingPointsAndRemovesCollinearPoints)
{
    const std::vector<PathPoint> input = {
        { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 2.0f, 0.0f }, { 2.0f, 1.0f },
        { 2.0f, 2.0f }, { 1.0f, 2.0f }, { 0.0f, 2.0f }, { 0.0f, 1.0f },
    };

    const auto response = runOptimizePath(makePath(input), 0.0);

    ASSERT_FALSE(response->isError()) << response->error();
    EXPECT_EQ(indices(response), (std::vector<int32_t>{ 0, 2, 4, 6 }));
}

TEST(OptimizePathTest, PreservesPathsWithAtMostTwoPoints)
{
    const auto twoPointResponse = runOptimizePath(makePath({ { 1.0f, 2.0f }, { 3.0f, 4.0f } }), 100.0);
    ASSERT_FALSE(twoPointResponse->isError()) << twoPointResponse->error();
    EXPECT_EQ(indices(twoPointResponse), (std::vector<int32_t>{ 0, 1 }));

    const auto singlePointResponse = runOptimizePath(makePath({ { 1.0f, 2.0f } }), 100.0);
    ASSERT_FALSE(singlePointResponse->isError()) << singlePointResponse->error();
    EXPECT_EQ(indices(singlePointResponse), (std::vector<int32_t>{ 0 }));
}

TEST(OptimizePathTest, RejectsMalformedPathsAndInvalidMaximumError)
{
    Buffer<uint8_t> malformedPath(7);
    const auto malformed = executeCommand(binJSONObject({
        { "command", binJSON("optimizePath") },
        { "path", binJSON(malformedPath) },
        { "maxError", binJSON(1.0) },
    }));
    EXPECT_TRUE(malformed->isError());

    const auto negativeError = runOptimizePath(makePath({ { 0.0f, 0.0f } }), -1.0);
    EXPECT_TRUE(negativeError->isError());

    const auto nonFinitePoint = runOptimizePath(
        makePath({ { 0.0f, 0.0f }, { std::numeric_limits<float>::infinity(), 1.0f } }), 1.0);
    EXPECT_TRUE(nonFinitePoint->isError());
}

}
