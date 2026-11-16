
#include "../buffer.hpp"
#include "../raster.hpp"
#include "../binjson.hpp"
#include "../commands.hpp"
#include "../flood.hpp"

#include <vector>

struct LabelInfo
{
    int32_t index;
    int32_t pixelCount;
    PixelRectangle bbox;
    PixelLocation topLeft;
};


BinJSON::Object binJSON(const LabelInfo& value) {
    return binJSONObject({
        { "index", binJSON(value.index) },
        { "pixelCount", binJSON(value.pixelCount) },
        { "topLeft", binJSON(value.topLeft) },
        { "bbox", binJSON(value.bbox) },
    });
}


static inline bool isBoundary(const uint8_t* srcPixel, const int16_t* dstPixel)
{
    return *srcPixel == 0 || *dstPixel != -1;
}

static inline void labelPixel(
    int16_t* dstPixel,
    int32_t x,
    int32_t y,
    int16_t label,
    LabelInfo& info)
{
    CUCUT_ASSERT(*dstPixel == -1, "Only unvisited pixels can be labeled");
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
    int32_t dstStride)
{
    uint8_t mask = 0;
    const uint8_t* srcRow = srcData + y * srcStride;
    const auto* dstRow = reinterpret_cast<const int16_t*>(
        reinterpret_cast<const uint8_t*>(dstData) + y * dstStride);

    if (y > 0) {
        const uint8_t* srcTop = srcRow - srcStride;
        const auto* dstTop = reinterpret_cast<const int16_t*>(
            reinterpret_cast<const uint8_t*>(dstRow) - dstStride);
        if (x + 1 >= width || isBoundary(srcTop + x + 1, dstTop + x + 1)) mask |= 1u << 5;
        if (isBoundary(srcTop + x, dstTop + x)) mask |= 1u << 6;
        if (x == 0 || isBoundary(srcTop + x - 1, dstTop + x - 1)) mask |= 1u << 7;
    } else {
        mask |= (1u << 5) | (1u << 6) | (1u << 7);
    }

    if (x + 1 >= width || isBoundary(srcRow + x + 1, dstRow + x + 1)) mask |= 1u << 3;
    if (x == 0 || isBoundary(srcRow + x - 1, dstRow + x - 1)) mask |= 1u << 4;

    if (y + 1 < height) {
        const uint8_t* srcBottom = srcRow + srcStride;
        const auto* dstBottom = reinterpret_cast<const int16_t*>(
            reinterpret_cast<const uint8_t*>(dstRow) + dstStride);
        if (x + 1 >= width || isBoundary(srcBottom + x + 1, dstBottom + x + 1)) mask |= 1u << 0;
        if (isBoundary(srcBottom + x, dstBottom + x)) mask |= 1u << 1;
        if (x == 0 || isBoundary(srcBottom + x - 1, dstBottom + x - 1)) mask |= 1u << 2;
    } else {
        mask |= (1u << 0) | (1u << 1) | (1u << 2);
    }
    return mask;
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
    Buffer<LabelRaster> dstBuffer = LabelRaster::create(
        width, height, static_cast<int16_t>(-1));
    LabelRaster* dstRaster = dstBuffer.raw();
    const int32_t dstStride = dstRaster->stride;
    uint8_t* srcData = srcRaster->data;
    auto* dstData = reinterpret_cast<int16_t*>(dstRaster->data);
    int32_t labelCount = 0;
    std::vector<int32_t> pendingPixels;
    const FloodFillJobs* const jobs = is8Neighborhood ? flood8 : flood4;

    labelInfos->length(0);

    for (int32_t y = 0; y < height; ++y) {
        uint8_t* srcPixel = srcData + y * srcStride;
        auto* dstPixel = reinterpret_cast<int16_t*>(
            reinterpret_cast<uint8_t*>(dstData) + y * dstStride);
        for (int32_t x = 0; x < width; ++x, ++srcPixel, ++dstPixel) {
            if (*srcPixel == 0 || *dstPixel != -1) {
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
            const int16_t label = static_cast<int16_t>(labelCount++);
            labelPixel(dstPixel, x, y, label, info);

            int32_t currentX = x;
            int32_t currentY = y;
            while (true) {
                const uint8_t mask = neighborhoodMask(srcData, dstData,
                    currentX, currentY, width, height, srcStride, dstStride);
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
                    if (*queuedSrc != 0 && *queuedDst == -1) {
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
                    if (*nextSrc != 0 && *nextDst == -1) {
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
