#include "../commands.hpp"
#include "../raster.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

/** @brief Integer raster location used while tracing a boundary. */
struct Point
{
    int32_t x;
    int32_t y;
};

/** @brief Floating-point midpoint emitted for one boundary pixel pair. */
struct PathPoint
{
    float x;
    float y;
};

/** @brief Read an exact, in-range integer from a binJSON number. */
static bool readInt32(const BinJSON::Any& value, int32_t& result)
{
    if (!value || value->getType() != BinJSON::BinJSONType::NUMBER) {
        return false;
    }
    const double number = value->number();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < std::numeric_limits<int32_t>::min()
        || number > std::numeric_limits<int32_t>::max()) {
        return false;
    }
    result = static_cast<int32_t>(number);
    return true;
}

/** @brief Read an integer pixel coordinate from an object with x and y fields. */
static bool readPoint(const BinJSON::Any& value, Point& result)
{
    return value && readInt32(value->get("x"), result.x)
        && readInt32(value->get("y"), result.y);
}

/** @brief Read a distance pixel without assuming row alignment. */
static float distanceAt(const Raster<float>* raster, int32_t x, int32_t y)
{
    const auto* row = reinterpret_cast<const uint8_t*>(raster->data) + y * raster->stride;
    float value;
    std::memcpy(&value, row + x * static_cast<int32_t>(sizeof(float)), sizeof(value));
    return value;
}

/** @brief Check whether an integer location lies inside the raster. */
static bool inBounds(const Raster<float>* raster, const Point& point)
{
    return point.x >= 0 && point.x < raster->width
        && point.y >= 0 && point.y < raster->height;
}

/** @brief Find the first inside/outside radius pair above an outer-edge start. */
static bool findOuterStart(
    const Raster<float>* raster,
    const Point& start,
    float radius,
    Point& inside,
    Point& outside,
    std::string& error)
{
    if (distanceAt(raster, start.x, start.y) != 0.0f) {
        error = "Separation outer startingPoint must be on an object pixel";
        return false;
    }

    inside = start;
    for (int32_t y = start.y - 1; y >= 0; --y) {
        outside = { start.x, y };
        if (distanceAt(raster, outside.x, outside.y) >= radius) {
            return true;
        }
        inside = outside;
    }
    error = "Separation reached raster edge while finding outer path";
    return false;
}

/** @brief Find an adjacent radius crossing around a fallback inner-edge point. */
static bool findCrossingNear(
    const Raster<float>* raster,
    const Point& center,
    float radius,
    Point& inside,
    Point& outside,
    std::string& error)
{
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            const Point candidate{ center.x + dx, center.y + dy };
            if (!inBounds(raster, candidate)) {
                error = "Separation reached raster edge while finding inner path";
                return false;
            }
            if (distanceAt(raster, candidate.x, candidate.y) > radius) {
                continue;
            }
            constexpr Point neighbors[] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
            for (const Point& offset : neighbors) {
                const Point next{ candidate.x + offset.x, candidate.y + offset.y };
                if (!inBounds(raster, next)) {
                    error = "Separation reached raster edge while finding inner path";
                    return false;
                }
                if (distanceAt(raster, next.x, next.y) >= radius) {
                    inside = candidate;
                    outside = next;
                    return true;
                }
            }
        }
    }
    return false;
}

/** @brief Find the radius crossing from a point inside a hole. */
static bool findInnerStart(
    const Raster<float>* raster,
    const Point& start,
    float radius,
    Point& inside,
    Point& outside,
    std::string& error)
{
    const float startDistance = distanceAt(raster, start.x, start.y);
    if (!(startDistance > 0.0f && startDistance < radius)) {
        error = "Separation inner startingPoint must be outside the object and closer than radius";
        return false;
    }

    constexpr Point neighbors[] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    Point current = start;
    while (true) {
        Point nextPoint = current;
        float nextDistance = distanceAt(raster, current.x, current.y);
        for (const Point& offset : neighbors) {
            const Point next{ current.x + offset.x, current.y + offset.y };
            if (!inBounds(raster, next)) {
                error = "Separation reached raster edge while finding inner path";
                return false;
            }
            const float distance = distanceAt(raster, next.x, next.y);
            if (distance > nextDistance) {
                nextPoint = next;
                nextDistance = distance;
            }
        }
        if (nextPoint.x == current.x && nextPoint.y == current.y) {
            break;
        }
        if (nextDistance >= radius) {
            inside = current;
            outside = nextPoint;
            return true;
        }
        current = nextPoint;
    }

    const int64_t pixelCount = static_cast<int64_t>(raster->width) * raster->height;
    if (pixelCount > INT32_MAX) {
        error = "Separation raster is too large";
        return false;
    }
    std::vector<uint8_t> visited(static_cast<int32_t>(pixelCount), 0);
    std::vector<Point> pending;
    pending.emplace_back(start);
    visited[start.y * raster->width + start.x] = 1;
    for (int32_t index = 0; index < static_cast<int32_t>(pending.size()); ++index) {
        const Point currentPoint = pending[index];
        if (distanceAt(raster, currentPoint.x, currentPoint.y) >= radius
            && findCrossingNear(raster, currentPoint, radius, inside, outside, error)) {
            return true;
        }
        if (!error.empty()) {
            return false;
        }
        for (const Point& offset : neighbors) {
            const Point next{ currentPoint.x + offset.x, currentPoint.y + offset.y };
            if (!inBounds(raster, next)) {
                error = "Separation reached raster edge while finding inner path";
                return false;
            }
            const int32_t nextIndex = next.y * raster->width + next.x;
            if (!visited[nextIndex] && distanceAt(raster, next.x, next.y) > 0.0f) {
                visited[nextIndex] = 1;
                pending.emplace_back(next);
            }
        }
    }
    error = "Separation could not find the requested inner radius";
    return false;
}

/** @brief Validate a separation request and return its traced path as float pairs. */
static BinJSON::Any separation(BinJSON::Any command)
{
    const auto& distanceValue = command->get("distance");
    if (!distanceValue || distanceValue->getType() != BinJSON::BinJSONType::BINARY) {
        return BinJSON::makeError("Separation distance must be a raster binary buffer");
    }
    Buffer<> distanceBuffer = distanceValue->buffer();
    if (distanceBuffer.size() < static_cast<int32_t>(offsetof(Raster<float>, data))) {
        return BinJSON::makeError("Separation distance raster is smaller than its header");
    }

    auto* raster = distanceBuffer.raw<Raster<float>*>();
    if (raster->width <= 0 || raster->height <= 0
        || raster->pixelBytes != static_cast<int32_t>(sizeof(float))
        || raster->stride < static_cast<int64_t>(raster->width) * static_cast<int64_t>(sizeof(float))) {
        return BinJSON::makeError("Separation distance raster has invalid dimensions or pixel size");
    }
    const int64_t expectedSize = static_cast<int64_t>(offsetof(Raster<float>, data))
        + static_cast<int64_t>(raster->stride) * raster->height;
    if (expectedSize > distanceBuffer.size()) {
        return BinJSON::makeError("Separation distance raster data is truncated");
    }

    const auto& radiusValue = command->get("radius");
    if (!radiusValue || radiusValue->getType() != BinJSON::BinJSONType::NUMBER
        || !std::isfinite(radiusValue->number()) || radiusValue->number() <= 0.0
        || radiusValue->number() > std::numeric_limits<float>::max()) {
        return BinJSON::makeError("Separation radius must be a finite positive number");
    }
    const float radius = static_cast<float>(radiusValue->number());
    if (!std::isfinite(radius) || radius <= 0.0f) {
        return BinJSON::makeError("Separation radius is outside the supported range");
    }
    const auto& directionValue = command->get("direction");
    const auto& edgeValue = command->get("edge");
    if (!directionValue || directionValue->getType() != BinJSON::BinJSONType::STRING
        || (directionValue->string() != "cw" && directionValue->string() != "ccw")) {
        return BinJSON::makeError("Separation direction must be 'cw' or 'ccw'");
    }
    if (!edgeValue || edgeValue->getType() != BinJSON::BinJSONType::STRING
        || (edgeValue->string() != "inner" && edgeValue->string() != "outer")) {
        return BinJSON::makeError("Separation edge must be 'inner' or 'outer'");
    }
    Point start{};
    if (!readPoint(command->get("startingPoint"), start) || !inBounds(raster, start)) {
        return BinJSON::makeError("Separation startingPoint is invalid or outside the raster");
    }

    const int64_t pixelCount = static_cast<int64_t>(raster->width) * raster->height;
    if (pixelCount > INT32_MAX) {
        return BinJSON::makeError("Separation raster is too large");
    }
    for (int32_t y = 0; y < raster->height; ++y) {
        for (int32_t x = 0; x < raster->width; ++x) {
            const float distance = distanceAt(raster, x, y);
            if (std::isnan(distance) || distance < 0.0f) {
                return BinJSON::makeError("Separation distance raster contains an invalid distance");
            }
        }
    }

    Point a{};
    Point b{};
    std::string error;
    const bool foundStart = edgeValue->string() == "outer"
        ? findOuterStart(raster, start, radius, a, b, error)
        : findInnerStart(raster, start, radius, a, b, error);
    if (!foundStart) {
        return BinJSON::makeError(error);
    }

    std::vector<uint8_t> visited(static_cast<int32_t>(pixelCount), 0);
    std::vector<PathPoint> path;
    const bool clockwise = directionValue->string() == "cw";
    const int64_t maximumSteps = pixelCount * 8;
    for (int64_t step = 0; step < maximumSteps; ++step) {
        if (!inBounds(raster, a) || !inBounds(raster, b)) {
            return BinJSON::makeError("Separation reached raster edge while tracing path");
        }
        const int32_t aIndex = a.y * raster->width + a.x;
        const int32_t bIndex = b.y * raster->width + b.x;
        if (visited[aIndex] && visited[bIndex]) {
            break;
        }
        visited[aIndex] = 1;
        visited[bIndex] = 1;
        path.emplace_back(PathPoint{
            (static_cast<float>(a.x) + static_cast<float>(b.x)) * 0.5f,
            (static_cast<float>(a.y) + static_cast<float>(b.y)) * 0.5f,
        });

        const int32_t vx = b.x - a.x;
        const int32_t vy = b.y - a.y;
        if (std::abs(vx) + std::abs(vy) != 1) {
            return BinJSON::makeError("Separation encountered non-adjacent boundary pixels");
        }
        const Point direction = clockwise ? Point{ vy, -vx } : Point{ -vy, vx };
        const Point aPlus{ a.x + direction.x, a.y + direction.y };
        if (!inBounds(raster, aPlus)) {
            return BinJSON::makeError("Separation reached raster edge while tracing path");
        }
        if (distanceAt(raster, aPlus.x, aPlus.y) >= radius) {
            b = aPlus;
        } else {
            const Point bPlus{ b.x + direction.x, b.y + direction.y };
            if (!inBounds(raster, bPlus)) {
                return BinJSON::makeError("Separation reached raster edge while tracing path");
            }
            if (distanceAt(raster, bPlus.x, bPlus.y) >= radius) {
                a = aPlus;
                b = bPlus;
            } else {
                a = bPlus;
            }
        }
    }
    if (path.empty() || static_cast<int64_t>(path.size()) >= maximumSteps) {
        return BinJSON::makeError("Separation failed to close the path");
    }

    const int64_t resultSize = static_cast<int64_t>(path.size()) * 2 * sizeof(float);
    if (resultSize > RawBuffer::maximumCapacity) {
        return BinJSON::makeError("Separation path is too large");
    }
    Buffer<float> pathBuffer(static_cast<int32_t>(resultSize));
    float* output = pathBuffer.raw<float*>();
    for (int32_t index = 0; index < static_cast<int32_t>(path.size()); ++index) {
        output[index * 2] = path[index].x;
        output[index * 2 + 1] = path[index].y;
    }
    return binJSONObject({ { "path", binJSON(pathBuffer) } });
}

} // namespace

REGISTER_COMMAND(separation);
