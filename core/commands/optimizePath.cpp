#include "../commands.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

/** @brief One floating-point point from the path buffer. */
struct PathPoint
{
    float x;
    float y;
};

/** @brief Inclusive endpoint positions of a path arc being simplified. */
struct Range
{
    int32_t begin;
    int32_t end;
};

/** @brief Return the squared distance from a point to a finite line segment. */
static double distanceSquaredToSegment(const PathPoint& point, const PathPoint& begin, const PathPoint& end)
{
    const double dx = static_cast<double>(end.x) - begin.x;
    const double dy = static_cast<double>(end.y) - begin.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared == 0.0) {
        const double pointDx = static_cast<double>(point.x) - begin.x;
        const double pointDy = static_cast<double>(point.y) - begin.y;
        return pointDx * pointDx + pointDy * pointDy;
    }

    const double pointDx = static_cast<double>(point.x) - begin.x;
    const double pointDy = static_cast<double>(point.y) - begin.y;
    const double projection = std::clamp(
        (pointDx * dx + pointDy * dy) / lengthSquared, 0.0, 1.0);
    const double errorX = pointDx - projection * dx;
    const double errorY = pointDy - projection * dy;
    return errorX * errorX + errorY * errorY;
}

/** @brief Mark retained original points using iterative Douglas-Peucker simplification. */
static void simplifyArc(
    const std::vector<PathPoint>& points,
    const std::vector<int32_t>& arc,
    double maximumErrorSquared,
    std::vector<uint8_t>& keep)
{
    if (arc.size() < 2) {
        return;
    }

    std::vector<Range> pending;
    pending.push_back({ 0, static_cast<int32_t>(arc.size()) - 1 });
    while (!pending.empty()) {
        const Range range = pending.back();
        pending.pop_back();
        const PathPoint& begin = points[arc[range.begin]];
        const PathPoint& end = points[arc[range.end]];
        double greatestErrorSquared = maximumErrorSquared;
        int32_t split = -1;
        for (int32_t position = range.begin + 1; position < range.end; ++position) {
            const double errorSquared = distanceSquaredToSegment(
                points[arc[position]], begin, end);
            if (errorSquared > greatestErrorSquared) {
                greatestErrorSquared = errorSquared;
                split = position;
            }
        }
        if (split >= 0) {
            keep[arc[split]] = 1;
            pending.push_back({ range.begin, split });
            pending.push_back({ split, range.end });
        }
    }
}

/** @brief Validate and simplify a closed path request. */
static BinJSON::Any optimizePath(BinJSON::Any command)
{
    const auto& pathValue = command->get("path");
    if (!pathValue || pathValue->getType() != BinJSON::BinJSONType::BINARY) {
        return BinJSON::makeError("OptimizePath path must be a binary buffer of float coordinate pairs");
    }
    const Buffer<> pathBuffer = pathValue->buffer();
    if (pathBuffer.size() == 0 || pathBuffer.size() % (2 * static_cast<int32_t>(sizeof(float))) != 0) {
        return BinJSON::makeError("OptimizePath path must contain complete float coordinate pairs");
    }

    const auto& errorValue = command->get("maxError");
    if (!errorValue || errorValue->getType() != BinJSON::BinJSONType::NUMBER
        || !std::isfinite(errorValue->number()) || errorValue->number() < 0.0) {
        return BinJSON::makeError("OptimizePath maxError must be a finite non-negative number");
    }

    const int32_t pointCount = pathBuffer.size() / (2 * static_cast<int32_t>(sizeof(float)));
    std::vector<PathPoint> points(pointCount);
    const uint8_t* input = pathBuffer.raw<const uint8_t*>();
    for (int32_t index = 0; index < pointCount; ++index) {
        std::memcpy(&points[index].x, input + index * 2 * sizeof(float), sizeof(float));
        std::memcpy(&points[index].y, input + (index * 2 + 1) * sizeof(float), sizeof(float));
        if (!std::isfinite(points[index].x) || !std::isfinite(points[index].y)) {
            return BinJSON::makeError("OptimizePath path contains a non-finite coordinate");
        }
    }

    std::vector<uint8_t> keep(pointCount, 0);
    if (pointCount <= 2) {
        std::fill(keep.begin(), keep.end(), 1);
    } else {
        int32_t opposite = 1;
        double greatestDistanceSquared = -1.0;
        for (int32_t index = 1; index < pointCount; ++index) {
            const double dx = static_cast<double>(points[index].x) - points[0].x;
            const double dy = static_cast<double>(points[index].y) - points[0].y;
            const double distanceSquared = dx * dx + dy * dy;
            if (distanceSquared > greatestDistanceSquared) {
                greatestDistanceSquared = distanceSquared;
                opposite = index;
            }
        }

        std::vector<int32_t> firstArc;
        firstArc.reserve(opposite + 1);
        for (int32_t index = 0; index <= opposite; ++index) {
            firstArc.push_back(index);
        }
        std::vector<int32_t> secondArc;
        secondArc.reserve(pointCount - opposite + 1);
        for (int32_t index = opposite; index < pointCount; ++index) {
            secondArc.push_back(index);
        }
        secondArc.push_back(0);

        keep[0] = 1;
        keep[opposite] = 1;
        const double maximumError = errorValue->number();
        const double maximumErrorSquared = maximumError * maximumError;
        simplifyArc(points, firstArc, maximumErrorSquared, keep);
        simplifyArc(points, secondArc, maximumErrorSquared, keep);
    }

    int32_t outputCount = 0;
    for (const uint8_t shouldKeep : keep) {
        outputCount += shouldKeep != 0;
    }
    if (outputCount > RawBuffer::maximumCapacity / (2 * static_cast<int32_t>(sizeof(float)))
        || outputCount > RawBuffer::maximumCapacity / static_cast<int32_t>(sizeof(int32_t))) {
        return BinJSON::makeError("OptimizePath output is too large");
    }

    Buffer<float> outputPath(outputCount * 2 * static_cast<int32_t>(sizeof(float)));
    Buffer<int32_t> outputIndices(outputCount * static_cast<int32_t>(sizeof(int32_t)));
    float* outputCoordinates = outputPath.raw<float*>();
    int32_t* outputMapping = outputIndices.raw<int32_t*>();
    int32_t outputIndex = 0;
    for (int32_t index = 0; index < pointCount; ++index) {
        if (!keep[index]) {
            continue;
        }
        outputCoordinates[outputIndex * 2] = points[index].x;
        outputCoordinates[outputIndex * 2 + 1] = points[index].y;
        outputMapping[outputIndex] = index;
        ++outputIndex;
    }

    return binJSONObject({
        { "path", binJSON(outputPath) },
        { "indices", binJSON(outputIndices) },
    });
}

} // namespace

REGISTER_COMMAND(optimizePath);
