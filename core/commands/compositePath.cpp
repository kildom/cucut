#include "../commands.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <queue>
#include <vector>

namespace {

/** @brief A path point represented with double-precision coordinates. */
struct Point
{
    double x;
    double y;
};

/** @brief A candidate arc and its fit error. */
struct Arc
{
    bool valid = false;
    double centerX = 0.0;
    double centerY = 0.0;
    double meanSquaredError = 0.0;
};

/** @brief A retained junction in the cyclic path list. */
struct Node
{
    int32_t pointIndex;
    int32_t previous;
    int32_t next;
    bool active = true;
    bool hasArc = false;
    double centerX = 0.0;
    double centerY = 0.0;
};

/** @brief A valid replacement of two adjacent segments by one arc. */
struct Candidate
{
    Arc arc;
    int32_t middle;
    int32_t left;
    int32_t right;
};

/** @brief Prioritize candidate arcs with the lowest mean squared error. */
struct CandidateOrder
{
    bool operator()(const Candidate& left, const Candidate& right) const
    {
        if (left.arc.meanSquaredError != right.arc.meanSquaredError) {
            return left.arc.meanSquaredError > right.arc.meanSquaredError;
        }
        return left.middle > right.middle;
    }
};

/** @brief A path point transformed into chord-aligned coordinates. */
struct LocalPoint
{
    double x;
    double y;
};

/** @brief Inclusive range in an ordered collection of path indices. */
struct Range
{
    int32_t begin;
    int32_t end;
};

/** @brief Return squared distance from a point to a finite line segment. */
static double distanceSquaredToSegment(const Point& point, const Point& begin, const Point& end)
{
    const double dx = end.x - begin.x;
    const double dy = end.y - begin.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared == 0.0) {
        const double pointDx = point.x - begin.x;
        const double pointDy = point.y - begin.y;
        return pointDx * pointDx + pointDy * pointDy;
    }
    const double pointDx = point.x - begin.x;
    const double pointDy = point.y - begin.y;
    const double projection = std::clamp(
        (pointDx * dx + pointDy * dy) / lengthSquared, 0.0, 1.0);
    const double errorX = pointDx - projection * dx;
    const double errorY = pointDy - projection * dy;
    return errorX * errorX + errorY * errorY;
}

/** @brief Normalize an angle to the range [0, 2 pi). */
static double normalizePositiveAngle(double angle)
{
    const double fullTurn = 2.0 * std::acos(-1.0);
    angle = std::fmod(angle, fullTurn);
    return angle < 0.0 ? angle + fullTurn : angle;
}

/** @brief Return distance to the finite minor arc defined by a chord and center offset. */
static double arcDistance(const LocalPoint& point, double halfChord, double centerOffset)
{
    const double radius = std::hypot(halfChord, centerOffset);
    const double startAngle = std::atan2(-centerOffset, -halfChord);
    const double endAngle = std::atan2(-centerOffset, halfChord);
    double sweep = endAngle - startAngle;
    const double pi = std::acos(-1.0);
    if (sweep > pi) {
        sweep -= 2.0 * pi;
    } else if (sweep < -pi) {
        sweep += 2.0 * pi;
    }

    const double relativeAngle = std::atan2(point.y - centerOffset, point.x);
    const double directedAngle = sweep >= 0.0
        ? normalizePositiveAngle(relativeAngle - startAngle)
        : normalizePositiveAngle(startAngle - relativeAngle);
    const double sweepMagnitude = std::abs(sweep);
    if (directedAngle <= sweepMagnitude + 1e-12) {
        return std::abs(std::hypot(point.x, point.y - centerOffset) - radius);
    }

    const double startDx = point.x + halfChord;
    const double startDy = point.y;
    const double endDx = point.x - halfChord;
    const double endDy = point.y;
    return std::sqrt(std::min(startDx * startDx + startDy * startDy, endDx * endDx + endDy * endDy));
}

/** @brief Compute the center's chord-normal offset for a given radius. */
static double perpendicularOffset(double radius, double halfChord)
{
    const double ratio = halfChord / radius;
    return radius * std::sqrt(std::max(0.0, 1.0 - ratio * ratio));
}

/** @brief Fit the lowest-error minor arc between two retained junctions. */
static Arc fitArc(
    const std::vector<Point>& points,
    int32_t firstIndex,
    int32_t lastIndex,
    double maximumError,
    double minimumRadius,
    double maximumRadius)
{
    const Point& first = points[firstIndex];
    const Point& last = points[lastIndex];
    const double chordX = last.x - first.x;
    const double chordY = last.y - first.y;
    const double chordLength = std::hypot(chordX, chordY);
    if (chordLength == 0.0 || maximumRadius < chordLength) {
        return {};
    }

    const double halfChord = chordLength * 0.5;
    const double unitX = chordX / chordLength;
    const double unitY = chordY / chordLength;
    const double normalX = -unitY;
    const double normalY = unitX;
    const double midpointX = (first.x + last.x) * 0.5;
    const double midpointY = (first.y + last.y) * 0.5;
    std::vector<LocalPoint> samples;
    int32_t index = firstIndex;
    do {
        const Point& point = points[index];
        const double dx = point.x - midpointX;
        const double dy = point.y - midpointY;
        samples.push_back({ dx * unitX + dy * unitY, dx * normalX + dy * normalY });
        index = (index + 1) % static_cast<int32_t>(points.size());
    } while (index != (lastIndex + 1) % static_cast<int32_t>(points.size()));

    const double lowerRadius = std::max(minimumRadius, chordLength);
    const double lowerOffset = perpendicularOffset(lowerRadius, halfChord);
    const double upperOffset = perpendicularOffset(maximumRadius, halfChord);
    if (lowerOffset > upperOffset) {
        return {};
    }

    Arc best;
    const double maximumErrorSquared = maximumError * maximumError;
    for (const double side : { -1.0, 1.0 }) {
        const double lower = side * lowerOffset;
        const double upper = side * upperOffset;
        const double intervalBegin = std::min(lower, upper);
        const double intervalEnd = std::max(lower, upper);

        const auto evaluate = [&](double centerOffset, bool& withinTolerance) {
            double totalSquaredError = 0.0;
            double greatestSquaredError = 0.0;
            for (const LocalPoint& sample : samples) {
                const double error = arcDistance(sample, halfChord, centerOffset);
                const double errorSquared = error * error;
                totalSquaredError += errorSquared;
                greatestSquaredError = std::max(greatestSquaredError, errorSquared);
            }
            withinTolerance = greatestSquaredError <= maximumErrorSquared;
            return totalSquaredError / static_cast<double>(samples.size());
        };

        double left = intervalBegin;
        double right = intervalEnd;
        constexpr double goldenRatio = 0.6180339887498948482;
        double firstProbe = right - goldenRatio * (right - left);
        double secondProbe = left + goldenRatio * (right - left);
        bool ignored = false;
        double firstValue = evaluate(firstProbe, ignored);
        double secondValue = evaluate(secondProbe, ignored);
        for (int32_t iteration = 0; iteration < 56; ++iteration) {
            if (firstValue <= secondValue) {
                right = secondProbe;
                secondProbe = firstProbe;
                secondValue = firstValue;
                firstProbe = right - goldenRatio * (right - left);
                firstValue = evaluate(firstProbe, ignored);
            } else {
                left = firstProbe;
                firstProbe = secondProbe;
                firstValue = secondValue;
                secondProbe = left + goldenRatio * (right - left);
                secondValue = evaluate(secondProbe, ignored);
            }
        }

        const double centerOffset = (left + right) * 0.5;
        bool withinTolerance = false;
        const double meanSquaredError = evaluate(centerOffset, withinTolerance);
        if (!withinTolerance || (best.valid && meanSquaredError >= best.meanSquaredError)) {
            continue;
        }

        const double centerX = midpointX + normalX * centerOffset;
        const double centerY = midpointY + normalY * centerOffset;
        const double floatLimit = std::numeric_limits<float>::max();
        if (!std::isfinite(centerX) || !std::isfinite(centerY)
            || std::abs(centerX) > floatLimit || std::abs(centerY) > floatLimit) {
            continue;
        }
        best = { true, centerX, centerY, meanSquaredError };
    }
    return best;
}

/** @brief Validate a composite-path request, simplify it, and serialize its junctions. */
static BinJSON::Any compositePath(BinJSON::Any command)
{
    const auto& pathValue = command->get("path");
    if (!pathValue || pathValue->getType() != BinJSON::BinJSONType::BINARY) {
        return BinJSON::makeError("CompositePath path must be a binary buffer of float coordinate pairs");
    }
    const Buffer<> pathBuffer = pathValue->buffer();
    if (pathBuffer.size() == 0 || pathBuffer.size() % (2 * static_cast<int32_t>(sizeof(float))) != 0) {
        return BinJSON::makeError("CompositePath path must contain complete float coordinate pairs");
    }

    const auto& indicesValue = command->get("indices");
    if (!indicesValue || indicesValue->getType() != BinJSON::BinJSONType::BINARY) {
        return BinJSON::makeError("CompositePath indices must be a binary buffer of int32 indices");
    }
    const Buffer<> indicesBuffer = indicesValue->buffer();
    if (indicesBuffer.size() == 0 || indicesBuffer.size() % static_cast<int32_t>(sizeof(int32_t)) != 0) {
        return BinJSON::makeError("CompositePath indices must contain complete int32 values");
    }

    const auto getFiniteNonNegativeNumber = [&](const char* name, double& output) {
        const auto& value = command->get(name);
        if (!value || value->getType() != BinJSON::BinJSONType::NUMBER
            || !std::isfinite(value->number()) || value->number() < 0.0) {
            return false;
        }
        output = value->number();
        return true;
    };
    double maximumError = 0.0;
    double minimumRadius = 0.0;
    double maximumRadius = 0.0;
    if (!getFiniteNonNegativeNumber("maxError", maximumError)) {
        return BinJSON::makeError("CompositePath maxError must be a finite non-negative number");
    }
    if (!getFiniteNonNegativeNumber("minRadius", minimumRadius)) {
        return BinJSON::makeError("CompositePath minRadius must be a finite non-negative number");
    }
    if (!getFiniteNonNegativeNumber("maxRadius", maximumRadius) || maximumRadius < minimumRadius) {
        return BinJSON::makeError("CompositePath maxRadius must be finite and at least minRadius");
    }

    const int32_t pointCount = pathBuffer.size() / (2 * static_cast<int32_t>(sizeof(float)));
    const int32_t nodeCount = indicesBuffer.size() / static_cast<int32_t>(sizeof(int32_t));
    if (nodeCount > pointCount) {
        return BinJSON::makeError("CompositePath indices contain more entries than the input path");
    }
    std::vector<Point> points(pointCount);
    const uint8_t* pathData = pathBuffer.raw<const uint8_t*>();
    for (int32_t index = 0; index < pointCount; ++index) {
        float x = 0.0f;
        float y = 0.0f;
        std::memcpy(&x, pathData + index * 2 * sizeof(float), sizeof(float));
        std::memcpy(&y, pathData + (index * 2 + 1) * sizeof(float), sizeof(float));
        if (!std::isfinite(x) || !std::isfinite(y)) {
            return BinJSON::makeError("CompositePath path contains a non-finite coordinate");
        }
        points[index] = { x, y };
    }

    const int32_t* indexData = indicesBuffer.raw<const int32_t*>();
    std::vector<uint8_t> keep(pointCount, 0);
    int32_t previousPointIndex = -1;
    for (int32_t index = 0; index < nodeCount; ++index) {
        const int32_t pointIndex = indexData[index];
        if (pointIndex < 0 || pointIndex >= pointCount || pointIndex <= previousPointIndex) {
            return BinJSON::makeError("CompositePath indices must be unique, increasing, and within the input path");
        }
        keep[pointIndex] = 1;
        previousPointIndex = pointIndex;
    }

    if (nodeCount == 1 && pointCount > 1) {
        std::fill(keep.begin(), keep.end(), 1);
    } else if (nodeCount >= 2) {
        const double maximumErrorSquared = maximumError * maximumError;
        for (int32_t edge = 0; edge < nodeCount; ++edge) {
            const int32_t beginIndex = indexData[edge];
            const int32_t endIndex = indexData[(edge + 1) % nodeCount];
            std::vector<int32_t> edgePoints;
            int32_t pointIndex = beginIndex;
            while (true) {
                edgePoints.push_back(pointIndex);
                if (pointIndex == endIndex) {
                    break;
                }
                pointIndex = (pointIndex + 1) % pointCount;
            }

            std::vector<Range> pending;
            pending.push_back({ 0, static_cast<int32_t>(edgePoints.size()) - 1 });
            while (!pending.empty()) {
                const Range range = pending.back();
                pending.pop_back();
                const Point& begin = points[edgePoints[range.begin]];
                const Point& end = points[edgePoints[range.end]];
                double greatestErrorSquared = maximumErrorSquared;
                int32_t split = -1;
                for (int32_t position = range.begin + 1; position < range.end; ++position) {
                    const double errorSquared = distanceSquaredToSegment(
                        points[edgePoints[position]], begin, end);
                    if (errorSquared > greatestErrorSquared) {
                        greatestErrorSquared = errorSquared;
                        split = position;
                    }
                }
                if (split >= 0) {
                    keep[edgePoints[split]] = 1;
                    pending.push_back({ range.begin, split });
                    pending.push_back({ split, range.end });
                }
            }
        }
    }

    std::vector<Node> nodes;
    for (int32_t index = 0; index < pointCount; ++index) {
        if (keep[index]) {
            nodes.push_back({ index, 0, 0 });
        }
    }
    const int32_t initialNodeCount = static_cast<int32_t>(nodes.size());
    for (int32_t index = 0; index < initialNodeCount; ++index) {
        nodes[index].previous = (index + initialNodeCount - 1) % initialNodeCount;
        nodes[index].next = (index + 1) % initialNodeCount;
    }

    std::priority_queue<Candidate, std::vector<Candidate>, CandidateOrder> candidates;
    const auto addCandidate = [&](int32_t middle) {
        if (initialNodeCount < 4 || !nodes[middle].active) {
            return;
        }
        const int32_t left = nodes[middle].previous;
        const int32_t right = nodes[middle].next;
        const Arc arc = fitArc(
            points,
            nodes[left].pointIndex,
            nodes[right].pointIndex,
            maximumError,
            minimumRadius,
            maximumRadius);
        if (arc.valid) {
            candidates.push({ arc, middle, left, right });
        }
    };
    for (int32_t index = 0; index < initialNodeCount; ++index) {
        addCandidate(index);
    }

    int32_t activeCount = initialNodeCount;
    while (activeCount > 3 && !candidates.empty()) {
        const Candidate candidate = candidates.top();
        candidates.pop();
        Node& middle = nodes[candidate.middle];
        if (!middle.active || middle.previous != candidate.left || middle.next != candidate.right) {
            continue;
        }
        Node& left = nodes[candidate.left];
        Node& right = nodes[candidate.right];
        middle.active = false;
        left.next = candidate.right;
        right.previous = candidate.left;
        left.hasArc = true;
        left.centerX = candidate.arc.centerX;
        left.centerY = candidate.arc.centerY;
        --activeCount;
        addCandidate(candidate.left);
        addCandidate(candidate.right);
    }

    if (activeCount > (RawBuffer::maximumCapacity / static_cast<int32_t>(sizeof(float)) - 2) / 4) {
        return BinJSON::makeError("CompositePath output is too large");
    }
    Buffer<float> output((activeCount * 4 + 2) * static_cast<int32_t>(sizeof(float)));
    float* outputValues = output.raw<float*>();
    int32_t outputIndex = 0;
    int32_t firstNode = 0;
    while (!nodes[firstNode].active) {
        ++firstNode;
    }
    int32_t currentNode = firstNode;
    do {
        const Point& junction = points[nodes[currentNode].pointIndex];
        outputValues[outputIndex++] = static_cast<float>(junction.x);
        outputValues[outputIndex++] = static_cast<float>(junction.y);
        if (nodes[currentNode].hasArc) {
            outputValues[outputIndex++] = static_cast<float>(nodes[currentNode].centerX);
            outputValues[outputIndex++] = static_cast<float>(nodes[currentNode].centerY);
        } else {
            outputValues[outputIndex++] = std::numeric_limits<float>::quiet_NaN();
            outputValues[outputIndex++] = std::numeric_limits<float>::quiet_NaN();
        }
        currentNode = nodes[currentNode].next;
    } while (currentNode != firstNode);
    const Point& firstJunction = points[nodes[firstNode].pointIndex];
    outputValues[outputIndex++] = static_cast<float>(firstJunction.x);
    outputValues[outputIndex++] = static_cast<float>(firstJunction.y);

    return binJSONObject({ { "path", binJSON(output) } });
}

} // namespace

REGISTER_COMMAND(compositePath);
