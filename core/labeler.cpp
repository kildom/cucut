
#include "buffer.hpp"
#include "raster.hpp"
#include "wasm.hpp"
#include "binjson.hpp"

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


Buffer<LabelRaster> labeler(
    Buffer<PixelMapRaster> srcBuffer,
    BinJSON::Array labelInfos,
    bool is8Neighborhood)
{
    // TODO: Runtime checks because source buffer may come from external source.
    LabelInfo labelInfo;
    PixelMapRaster* srcRaster = srcBuffer.raw();
    CUCUT_ASSERT(srcRaster->pixelBytes == 1, "Invalid PixelMapRaster");
    int32_t width = srcRaster->width;
    int32_t height = srcRaster->height;
    int32_t srcStride = srcRaster->stride;
    Buffer<LabelRaster> dstBuffer = LabelRaster::create(width, height, static_cast<int16_t>(-1));
    LabelRaster* dstRaster = dstBuffer.raw();
    int32_t labelCount = 0;

    labelInfos->length(0);

    for (int32_t y = 0; y < height; y++) {
        uint8_t* srcPixel = srcRaster->data + srcStride * y;
        auto* dstPixel = reinterpret_cast<int16_t*>(
            reinterpret_cast<uint8_t*>(dstRaster->data) + dstRaster->stride * y);
        for (int32_t x = 0; x < width; x++) {
            if (*srcPixel != 0 && *dstPixel == -1) {
                labelInfo.index = labelCount;
                labelInfo.pixelCount = 0;
                labelInfo.topLeft.x = x;
                labelInfo.topLeft.y = y;
                labelCount++;
                //labelObject(srcRaster, dstRaster, width, height, srcStride, dstStride, &labelInfo, x, y);
                labelInfos->get(labelInfo.index) = binJSON(labelInfo);
            }
            srcPixel++;
            dstPixel++;
        }
    }

    return dstBuffer;
}

BinJSON::Any labelerCommand(BinJSON::Any command) {
    BinJSON::Array labelInfos;
    auto srcBuffer = command->get("bitmap")->buffer<PixelMapRaster>();
    auto is8Neighborhood = command->get("is8Neighborhood")->boolean();
    auto resultRaster = labeler(srcBuffer, labelInfos, is8Neighborhood);
    return binJSONObject({
        { "labeled", binJSON(resultRaster) },
        { "info", labelInfos },
    });
}


/*WASM_EXPORT(labeler)
void* labelerWasm(void* bitmap, void** infosBuffer) {
    Buffer<> infos(*infosBuffer);
    auto result = labeler(Buffer<>(bitmap), infos);
    return result.shareRaw<void*>();
}
*/
