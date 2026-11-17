#include "includes.hpp"

namespace composite_path_test {

#include "../../commands.cpp"
#include "../../commands/compositePath.cpp"

static Buffer<float> makePath(const std::vector<Point>& points)
{
    Buffer<float> result(static_cast<int32_t>(points.size() * 2 * sizeof(float)));
    float* coordinates = result.raw<float*>();
    for (int32_t index = 0; index < static_cast<int32_t>(points.size()); ++index) {
        coordinates[index * 2] = static_cast<float>(points[index].x);
        coordinates[index * 2 + 1] = static_cast<float>(points[index].y);
    }
    return result;
}

static Buffer<int32_t> makeIndices(const std::vector<int32_t>& indices)
{
    Buffer<int32_t> result(static_cast<int32_t>(indices.size() * sizeof(int32_t)));
    std::memcpy(result.raw<void*>(), indices.data(), indices.size() * sizeof(int32_t));
    return result;
}

static BinJSON::Any runCompositePath(
    const Buffer<float>& path,
    const Buffer<int32_t>& indices,
    double maxError = 0.001,
    double minRadius = 2.0,
    double maxRadius = 3.0)
{
    return executeCommand(binJSONObject({
        { "command", binJSON("compositePath") },
        { "path", binJSON(path) },
        { "indices", binJSON(indices) },
        { "maxError", binJSON(maxError) },
        { "minRadius", binJSON(minRadius) },
        { "maxRadius", binJSON(maxRadius) },
    }));
}

static std::vector<float> output(const BinJSON::Any& response)
{
    const Buffer<float> buffer = response->get("path")->buffer<float>();
    const float* values = buffer.raw<const float*>();
    return std::vector<float>(values, values + buffer.size() / static_cast<int32_t>(sizeof(float)));
}

static std::vector<Point> samplePath()
{
    return {
        { 0.0, 0.0 },
        { 1.0, 2.0 - std::sqrt(3.0) },
        { 2.0, 0.0 },
        { 2.0, 2.0 },
        { 0.0, 2.0 },
    };
}

TEST(CompositePathTest, ReplacesCircularPathSectionAndEmitsEquidistantCenter)
{
    const auto response = runCompositePath(makePath(samplePath()), makeIndices({ 0, 1, 2, 3, 4 }));

    ASSERT_FALSE(response->isError()) << response->error();
    const auto values = output(response);
    ASSERT_EQ(values.size(), 4u * 4u + 2u);
    ASSERT_TRUE(std::isfinite(values[2]));
    ASSERT_TRUE(std::isfinite(values[3]));
    EXPECT_NEAR(values[2], 1.0, 1e-5);
    EXPECT_NEAR(values[3], -std::sqrt(3.0), 1e-5);

    const double firstRadius = std::hypot(values[0] - values[2], values[1] - values[3]);
    const double secondRadius = std::hypot(values[4] - values[2], values[5] - values[3]);
    EXPECT_NEAR(firstRadius, secondRadius, 1e-6);
    EXPECT_FLOAT_EQ(values[values.size() - 2], values[0]);
    EXPECT_FLOAT_EQ(values[values.size() - 1], values[1]);
}

TEST(CompositePathTest, LeavesSegmentsStraightWhenToleranceIsZero)
{
    const auto response = runCompositePath(
        makePath(samplePath()), makeIndices({ 0, 1, 2, 3, 4 }), 0.0);

    ASSERT_FALSE(response->isError()) << response->error();
    const auto values = output(response);
    ASSERT_EQ(values.size(), 4u * 5u + 2u);
    for (size_t index = 2; index < values.size() - 2; index += 4) {
        EXPECT_TRUE(std::isnan(values[index]));
        EXPECT_TRUE(std::isnan(values[index + 1]));
    }
}

TEST(CompositePathTest, RestoresPointsNeededToMeetTheRequestedLineTolerance)
{
    const auto response = runCompositePath(
        makePath(samplePath()), makeIndices({ 0, 2, 3, 4 }), 0.0, 0.0, 1.0);

    ASSERT_FALSE(response->isError()) << response->error();
    const auto values = output(response);
    EXPECT_EQ(values.size(), 4u * 5u + 2u);
}

TEST(CompositePathTest, RejectsInvalidIndicesAndRadiusLimits)
{
    const Buffer<float> path = makePath(samplePath());
    const auto repeatedIndex = runCompositePath(path, makeIndices({ 0, 1, 1, 3 }));
    EXPECT_TRUE(repeatedIndex->isError());

    const auto outOfRangeIndex = runCompositePath(path, makeIndices({ 0, 1, 2, 5 }));
    EXPECT_TRUE(outOfRangeIndex->isError());

    const auto reversedRadiusRange = runCompositePath(
        path, makeIndices({ 0, 1, 2, 3 }), 0.1, 3.0, 2.0);
    EXPECT_TRUE(reversedRadiusRange->isError());
}

}
