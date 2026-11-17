
#include "../buffer.hpp"
#include "../raster.hpp"
#include "../binjson.hpp"
#include "../commands.hpp"
#include "../flood.hpp"

#include <vector>

/*
Hole detection marks background connected to the raster boundary as -1. Remaining background pixels
belong to holes and are assigned unique labels starting at -2 when reached from an object. Hole metadata
includes its pixel count, first detected location, and bounding box. Objects inside holes remain independent
top-level labels; their pixels are excluded from the enclosing hole's count. The minimum int16 label is
reserved as the unvisited marker, so exhausting the available negative labels is an error.
*/

struct HoleInfo
{
    // Negative hole index, starts at -2 and goes down
    int32_t index;
    // Number of pixels occupied by the hole. If hole contains another objects, they are not counted here.
    int32_t pixelCount;
    // First detected pixel of the hole, it can be any pixel on the hole.
    PixelLocation location;
    // Bounding box of hole
    PixelRectangle bbox;
};

BinJSON::Object binJSON(const HoleInfo& value) {
    return binJSONObject({
        { "index", binJSON(value.index) },
        { "pixelCount", binJSON(value.pixelCount) },
        { "location", binJSON(value.location) },
        { "bbox", binJSON(value.bbox) },
    });
}

struct LabelInfo
{
    int32_t index;
    int32_t pixelCount;
    PixelRectangle bbox;
    PixelLocation topLeft;
    // Array of hole detected inside object.
    BinJSON::Array holes;
};


BinJSON::Object binJSON(const LabelInfo& value) {
    return binJSONObject({
        { "index", binJSON(value.index) },
        { "pixelCount", binJSON(value.pixelCount) },
        { "topLeft", binJSON(value.topLeft) },
        { "bbox", binJSON(value.bbox) },
        { "holes", value.holes },
    });
}


static inline bool isBoundary(
    const uint8_t* srcPixel,
    const int16_t* dstPixel,
    bool fillingObjects,
    int16_t unvisitedLabel)
{
    return (fillingObjects ? *srcPixel == 0 : *srcPixel != 0) || *dstPixel != unvisitedLabel;
}

static inline void labelPixel(
    int16_t* dstPixel,
    int32_t x,
    int32_t y,
    int16_t label,
    LabelInfo& info)
{
    CUCUT_ASSERT(*dstPixel == INT16_MIN, "Only unvisited pixels can be labeled");
    *dstPixel = label;
    ++info.pixelCount;
    if (x < info.bbox.begin.x) info.bbox.begin.x = static_cast<int16_t>(x);
    if (y < info.bbox.begin.y) info.bbox.begin.y = static_cast<int16_t>(y);
    if (x >= info.bbox.end.x) info.bbox.end.x = static_cast<int16_t>(x + 1);
    if (y >= info.bbox.end.y) info.bbox.end.y = static_cast<int16_t>(y + 1);
}

static inline uint8_t neighborhoodMask(
    const uint8_t* srcData,
    const int16_t* dstData,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    int32_t srcStride,
    int32_t dstStride,
    bool fillingObjects,
    int16_t unvisitedLabel)
{
    uint8_t mask = 0;
    const uint8_t* srcRow = srcData + y * srcStride;
    const auto* dstRow = reinterpret_cast<const int16_t*>(
        reinterpret_cast<const uint8_t*>(dstData) + y * dstStride);

    if (y > 0) {
        const uint8_t* srcTop = srcRow - srcStride;
        const auto* dstTop = reinterpret_cast<const int16_t*>(
            reinterpret_cast<const uint8_t*>(dstRow) - dstStride);
        if (x + 1 >= width || isBoundary(srcTop + x + 1, dstTop + x + 1, fillingObjects, unvisitedLabel)) mask |= 1u << 5;
        if (isBoundary(srcTop + x, dstTop + x, fillingObjects, unvisitedLabel)) mask |= 1u << 6;
        if (x == 0 || isBoundary(srcTop + x - 1, dstTop + x - 1, fillingObjects, unvisitedLabel)) mask |= 1u << 7;
    } else {
        mask |= (1u << 5) | (1u << 6) | (1u << 7);
    }

    if (x + 1 >= width || isBoundary(srcRow + x + 1, dstRow + x + 1, fillingObjects, unvisitedLabel)) mask |= 1u << 3;
    if (x == 0 || isBoundary(srcRow + x - 1, dstRow + x - 1, fillingObjects, unvisitedLabel)) mask |= 1u << 4;

    if (y + 1 < height) {
        const uint8_t* srcBottom = srcRow + srcStride;
        const auto* dstBottom = reinterpret_cast<const int16_t*>(
            reinterpret_cast<const uint8_t*>(dstRow) + dstStride);
        if (x + 1 >= width || isBoundary(srcBottom + x + 1, dstBottom + x + 1, fillingObjects, unvisitedLabel)) mask |= 1u << 0;
        if (isBoundary(srcBottom + x, dstBottom + x, fillingObjects, unvisitedLabel)) mask |= 1u << 1;
        if (x == 0 || isBoundary(srcBottom + x - 1, dstBottom + x - 1, fillingObjects, unvisitedLabel)) mask |= 1u << 2;
    } else {
        mask |= (1u << 0) | (1u << 1) | (1u << 2);
    }
    return mask;
}

static void fillRegion(
    const uint8_t* srcData,
    int16_t* dstData,
    int32_t width,
    int32_t height,
    int32_t srcStride,
    int32_t dstStride,
    int32_t startX,
    int32_t startY,
    bool fillingObjects,
    int16_t unvisitedLabel,
    int16_t label,
    int32_t& pixelCount,
    PixelRectangle& bbox,
    std::vector<int32_t>& pendingPixels,
    const FloodFillJobs* jobs)
{
    const auto setPixel = [&](int32_t x, int32_t y) {
        auto* const dstPixel = reinterpret_cast<int16_t*>(
            reinterpret_cast<uint8_t*>(dstData) + y * dstStride) + x;
        CUCUT_ASSERT(*dstPixel == unvisitedLabel, "Only unvisited pixels can be filled");
        *dstPixel = label;
        ++pixelCount;
        if (x < bbox.begin.x) bbox.begin.x = static_cast<int16_t>(x);
        if (y < bbox.begin.y) bbox.begin.y = static_cast<int16_t>(y);
        if (x >= bbox.end.x) bbox.end.x = static_cast<int16_t>(x + 1);
        if (y >= bbox.end.y) bbox.end.y = static_cast<int16_t>(y + 1);
    };

    setPixel(startX, startY);
    int32_t currentX = startX;
    int32_t currentY = startY;
    while (true) {
        const uint8_t mask = neighborhoodMask(srcData, dstData,
            currentX, currentY, width, height, srcStride, dstStride, fillingObjects, unvisitedLabel);
        const FloodFillJobs& action = jobs[mask];

        for (int16_t i = 0; i < action.count; ++i) {
            const int32_t queuedX = currentX + action.queue[i].x;
            const int32_t queuedY = currentY + action.queue[i].y;
            if (queuedX < 0 || queuedX >= width || queuedY < 0 || queuedY >= height) {
                continue;
            }
            const uint8_t* const queuedSrc = srcData + queuedY * srcStride + queuedX;
            auto* const queuedDst = reinterpret_cast<int16_t*>(
                reinterpret_cast<uint8_t*>(dstData) + queuedY * dstStride) + queuedX;
            const bool matches = fillingObjects ? *queuedSrc != 0 : *queuedSrc == 0;
            if (matches && *queuedDst == unvisitedLabel) {
                setPixel(queuedX, queuedY);
                pendingPixels.emplace_back(queuedY * width + queuedX);
            }
        }

        const int32_t nextX = currentX + action.next.x;
        const int32_t nextY = currentY + action.next.y;
        if (nextX >= 0 && nextX < width && nextY >= 0 && nextY < height) {
            const uint8_t* const nextSrc = srcData + nextY * srcStride + nextX;
            auto* const nextDst = reinterpret_cast<int16_t*>(
                reinterpret_cast<uint8_t*>(dstData) + nextY * dstStride) + nextX;
            const bool matches = fillingObjects ? *nextSrc != 0 : *nextSrc == 0;
            if (matches && *nextDst == unvisitedLabel) {
                setPixel(nextX, nextY);
                currentX = nextX;
                currentY = nextY;
                continue;
            }
        }

        if (pendingPixels.empty()) {
            break;
        }
        const int32_t nextIndex = pendingPixels.back();
        pendingPixels.pop_back();
        currentY = nextIndex / width;
        currentX = nextIndex - currentY * width;
    }
}

Buffer<LabelRaster> labeler(
    Buffer<PixelMapRaster> srcBuffer,
    BinJSON::Array labelInfos,
    bool is8Neighborhood)
{
    PixelMapRaster* srcRaster = srcBuffer.raw();
    CUCUT_ASSERT(srcRaster->pixelBytes == 1, "Invalid PixelMapRaster");
    const int32_t width = srcRaster->width;
    const int32_t height = srcRaster->height;
    const int32_t srcStride = srcRaster->stride;
    const int16_t unvisitedLabel = INT16_MIN;
    Buffer<LabelRaster> dstBuffer = LabelRaster::create(width, height, unvisitedLabel);
    LabelRaster* dstRaster = dstBuffer.raw();
    const int32_t dstStride = dstRaster->stride;
    uint8_t* srcData = srcRaster->data;
    auto* dstData = reinterpret_cast<int16_t*>(dstRaster->data);
    int32_t labelCount = 0;
    int32_t nextHoleIndex = -2;
    std::vector<int32_t> pendingPixels;
    std::vector<int32_t> pendingRegionPixels;
    const FloodFillJobs* const jobs = is8Neighborhood ? flood8 : flood4;

    labelInfos->length(0);

    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            if ((x == 0 || x == width - 1 || y == 0 || y == height - 1)
                && srcData[y * srcStride + x] == 0
                && reinterpret_cast<int16_t*>(
                    reinterpret_cast<uint8_t*>(dstData) + y * dstStride)[x] == unvisitedLabel) {
                int32_t ignoredPixelCount = 0;
                PixelRectangle ignoredBbox{ { static_cast<int16_t>(x), static_cast<int16_t>(y) },
                    { static_cast<int16_t>(x + 1), static_cast<int16_t>(y + 1) } };
                fillRegion(srcData, dstData, width, height, srcStride, dstStride,
                    x, y, false, unvisitedLabel, -1, ignoredPixelCount, ignoredBbox, pendingRegionPixels, jobs);
            }
        }
    }

    const auto detectAdjacentHoles = [&](int32_t x, int32_t y, LabelInfo& info) {
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                if ((dx == 0 && dy == 0) || (!is8Neighborhood && dx != 0 && dy != 0)) {
                    continue;
                }
                const int32_t holeX = x + dx;
                const int32_t holeY = y + dy;
                if (holeX < 0 || holeX >= width || holeY < 0 || holeY >= height) {
                    continue;
                }
                const uint8_t* const holeSrc = srcData + holeY * srcStride + holeX;
                auto* const holeDst = reinterpret_cast<int16_t*>(
                    reinterpret_cast<uint8_t*>(dstData) + holeY * dstStride) + holeX;
                if (*holeSrc != 0 || *holeDst != unvisitedLabel) {
                    continue;
                }
                if (nextHoleIndex <= INT16_MIN) {
                    fatalError("Label raster cannot represent more than 32766 holes.");
                }

                HoleInfo hole{};
                hole.index = nextHoleIndex--;
                hole.location = { static_cast<int16_t>(holeX), static_cast<int16_t>(holeY) };
                hole.bbox.begin = hole.location;
                hole.bbox.end = { static_cast<int16_t>(holeX + 1), static_cast<int16_t>(holeY + 1) };
                fillRegion(srcData, dstData, width, height, srcStride, dstStride,
                    holeX, holeY, false, unvisitedLabel, static_cast<int16_t>(hole.index),
                    hole.pixelCount, hole.bbox, pendingRegionPixels, jobs);
                info.holes->value.emplace_back(binJSON(hole));
            }
        }
    };

    for (int32_t y = 0; y < height; ++y) {
        uint8_t* srcPixel = srcData + y * srcStride;
        auto* dstPixel = reinterpret_cast<int16_t*>(
            reinterpret_cast<uint8_t*>(dstData) + y * dstStride);
        for (int32_t x = 0; x < width; ++x, ++srcPixel, ++dstPixel) {
            if (*srcPixel == 0 || *dstPixel != unvisitedLabel) {
                continue;
            }

            if (labelCount > INT16_MAX) {
                fatalError("Label raster cannot represent more than 32768 components.");
            }
            LabelInfo info{};
            info.index = labelCount;
            info.topLeft = { static_cast<int16_t>(x), static_cast<int16_t>(y) };
            info.bbox.begin = info.topLeft;
            info.bbox.end = { static_cast<int16_t>(x + 1), static_cast<int16_t>(y + 1) };
            info.holes = BinJSON::makeArray();
            const int16_t label = static_cast<int16_t>(labelCount++);
            labelPixel(dstPixel, x, y, label, info);

            int32_t currentX = x;
            int32_t currentY = y;
            while (true) {
                detectAdjacentHoles(currentX, currentY, info);
                const uint8_t mask = neighborhoodMask(srcData, dstData,
                    currentX, currentY, width, height, srcStride, dstStride, true, unvisitedLabel);
                const FloodFillJobs& action = jobs[mask];

                for (int16_t i = 0; i < action.count; ++i) {
                    const int32_t queuedX = currentX + action.queue[i].x;
                    const int32_t queuedY = currentY + action.queue[i].y;
                    if (queuedX < 0 || queuedX >= width || queuedY < 0 || queuedY >= height) {
                        continue;
                    }
                    uint8_t* const queuedSrc = srcData + queuedY * srcStride + queuedX;
                    auto* const queuedDst = reinterpret_cast<int16_t*>(
                        reinterpret_cast<uint8_t*>(dstData) + queuedY * dstStride) + queuedX;
                    if (*queuedSrc != 0 && *queuedDst == unvisitedLabel) {
                        labelPixel(queuedDst, queuedX, queuedY, label, info);
                        pendingPixels.emplace_back(queuedY * width + queuedX);
                    }
                }

                const int32_t nextX = currentX + action.next.x;
                const int32_t nextY = currentY + action.next.y;
                if (nextX >= 0 && nextX < width && nextY >= 0 && nextY < height) {
                    uint8_t* const nextSrc = srcData + nextY * srcStride + nextX;
                    auto* const nextDst = reinterpret_cast<int16_t*>(
                        reinterpret_cast<uint8_t*>(dstData) + nextY * dstStride) + nextX;
                    if (*nextSrc != 0 && *nextDst == unvisitedLabel) {
                        labelPixel(nextDst, nextX, nextY, label, info);
                        currentX = nextX;
                        currentY = nextY;
                        continue;
                    }
                }

                if (pendingPixels.empty()) {
                    break;
                }
                const int32_t nextIndex = pendingPixels.back();
                pendingPixels.pop_back();
                currentY = nextIndex / width;
                currentX = nextIndex - currentY * width;
            }

            labelInfos->value.emplace_back(binJSON(info));
        }
    }

    return dstBuffer;
}

static BinJSON::Any labeler(BinJSON::Any command) {
    BinJSON::Array labelInfos = BinJSON::makeArray();

    auto srcBuffer = command->get("bitmap")->buffer<PixelMapRaster>();
    auto is8Neighborhood = command->get("is8Neighborhood")->boolean();

    auto resultRaster = labeler(srcBuffer, labelInfos, is8Neighborhood);

    return binJSONObject({
        { "labeled", binJSON(resultRaster) },
        { "info", labelInfos },
    });
}

REGISTER_COMMAND(labeler);
