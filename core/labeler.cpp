
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


Buffer labeler(Buffer srcBuffer, std::shared_ptr<BinJSON::Value> &labelInfos) {
    LabelInfo labelInfo;
    PixelMapRaster* srcRaster = srcBuffer.as<PixelMapRaster*>();
    CUCUT_ASSERT(srcRaster->pixelBytes == 1, "Invalid PixelMapRaster");
    int32_t width = srcRaster->width;
    int32_t height = srcRaster->height;
    int32_t srcStride = srcRaster->stride;
    int32_t dstStride = LabelRaster::calcStride(width);
    Buffer dstBuffer(dstStride * height);
    LabelRaster* dstRaster = dstBuffer.as<LabelRaster*>();
    dstRaster->width = width;
    dstRaster->height = height;
    dstRaster->stride = dstStride;
    dstRaster->pixelBytes = LabelRaster::expectedPixelBytes;
    int32_t labelCount = 0;

    labelInfos = std::make_shared<BinJSON::Array>();

    std::memset(dstRaster->data, -1, height * dstStride);

    for (int32_t y = 0; y < height; y++) {
        uint8_t* srcPixel = srcRaster->data + srcStride * y;
        int16_t* dstPixel = dstRaster->data + dstStride * y;
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

std::shared_ptr<BinJSON::Number> binJSON(int32_t value) {
    return std::make_shared<BinJSON::Number>(value);
}

std::shared_ptr<BinJSON::Number> binJSON(int32_t value) {
    return std::make_shared<BinJSON::Number>(value);
}

std::shared_ptr<BinJSON::Object> binJSON(const PixelLocation& value) {
    auto result = std::make_shared<BinJSON::Object>();
    result->get("x") = binJSON(value.x);
    result->get("y") = binJSON(value.y);
    return result;
}

std::shared_ptr<BinJSON::Object> binJSON(const PixelRectangle& value) {
    return std::make_shared<BinJSON::Object>(std::map<std::string, std::shared_ptr<BinJSON::Value>> {
        { "begin", binJSON(value.begin) },
        { "end", binJSON(value.end) },
    });
}

std::shared_ptr<BinJSON::Object> binJSON(const LabelInfo& value) {
    return std::make_shared<BinJSON::Object>(std::map<std::string, std::shared_ptr<BinJSON::Value>> {
        { "index", binJSON(value.index) },
        { "pixelCount", binJSON(value.pixelCount) },
        { "topLeft", binJSON(value.topLeft) },
        { "bbox", binJSON(value.bbox) },
    });
}

std::shared_ptr<BinJSON::Binary> binJSON(const Buffer& buffer) {
    return std::make_shared<BinJSON::Binary>(buffer);
}

template<class T>
std::shared_ptr<BinJSON::Object> toBinJSONArray(const T* array, int32_t size) {
    std::vector<std::shared_ptr<BinJSON::Value>> vect(array, array + size);
    return std::make_shared<BinJSON::Array>(std::move(vect));
}

std::shared_ptr<BinJSON::Value> labelerCommand(std::shared_ptr<BinJSON::Value> command) {
    std::shared_ptr<BinJSON::Value> labelInfos;
    auto resultRaster = labeler(command->get("bitmap")->buffer(), labelInfos);
    return std::make_shared<BinJSON::Object>(std::map<std::string, std::shared_ptr<BinJSON::Value>> {
        { "labeled", binJSON(resultRaster) },
        { "info", labelInfos },
    });
}


/*WASM_EXPORT(labeler)
void* labelerWasm(void* bitmap, void** infosBuffer) {
    Buffer infos(*infosBuffer);
    auto result = labeler(Buffer(bitmap), infos);
    return result.inc();
}
*/
