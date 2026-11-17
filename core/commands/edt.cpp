#include "../commands.hpp"
#include "../raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {

/** @brief Parsed comparison operator and integer threshold for an EDT input. */
struct EdtCondition
{
    enum class Operator
    {
        EQUAL,
        NOT_EQUAL,
        SIGNED_LESS,
        SIGNED_GREATER,
        UNSIGNED_LESS,
        UNSIGNED_GREATER,
    };

    Operator op;
    uint64_t valueBits;
    int64_t signedValue;
};

/** @brief Read a finite integral BinJSON number in the signed 64-bit range. */
static bool readInteger(const BinJSON::Any& value, int64_t& result)
{
    if (!value || value->getType() != BinJSON::BinJSONType::NUMBER) {
        return false;
    }
    const double number = value->number();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < static_cast<double>(std::numeric_limits<int64_t>::min())
        || number >= 9223372036854775808.0) {
        return false;
    }
    result = static_cast<int64_t>(number);
    return true;
}

/** @brief Parse the pixel comparison and normalize its threshold to the pixel width. */
static bool readCondition(const BinJSON::Any& command, int32_t pixelBytes, EdtCondition& result)
{
    const auto& condition = command->get("objectCondition");
    const auto& value = command->get("objectValue");
    if (!condition || condition->getType() != BinJSON::BinJSONType::STRING) {
        return false;
    }

    const std::string& op = condition->string();
    if (op == "==") {
        result.op = EdtCondition::Operator::EQUAL;
    } else if (op == "!=") {
        result.op = EdtCondition::Operator::NOT_EQUAL;
    } else if (op == "s<") {
        result.op = EdtCondition::Operator::SIGNED_LESS;
    } else if (op == "s>") {
        result.op = EdtCondition::Operator::SIGNED_GREATER;
    } else if (op == "u<") {
        result.op = EdtCondition::Operator::UNSIGNED_LESS;
    } else if (op == "u>") {
        result.op = EdtCondition::Operator::UNSIGNED_GREATER;
    } else {
        return false;
    }

    int64_t numericValue;
    if (!readInteger(value, numericValue)) {
        return false;
    }

    if (pixelBytes == 8) {
        const auto& highValue = command->get("objectValueHi32");
        int64_t high = 0;
        if ((highValue && !highValue->isNull()
                && (!readInteger(highValue, high) || high < 0 || high > UINT32_MAX))
            || numericValue < 0 || static_cast<uint64_t>(numericValue) > UINT32_MAX) {
            return false;
        }
        result.valueBits = (static_cast<uint64_t>(high) << 32)
            | static_cast<uint32_t>(numericValue);
        std::memcpy(&result.signedValue, &result.valueBits, sizeof(result.signedValue));
        return true;
    }

    const int32_t bitCount = pixelBytes * 8;
    result.valueBits = static_cast<uint64_t>(numericValue)
        & ((static_cast<uint64_t>(1) << bitCount) - 1);
    result.signedValue = numericValue;
    return true;
}

/** @brief Read an integer pixel without alignment or aliasing assumptions. */
template<class T>
static T pixelValue(const uint8_t* address)
{
    T value;
    std::memcpy(&value, address, sizeof(T));
    return value;
}

/** @brief Read a pixel as an unsigned integer of its declared width. */
static uint64_t readUnsignedPixel(const uint8_t* address, int32_t pixelBytes)
{
    switch (pixelBytes) {
    case 1: return pixelValue<uint8_t>(address);
    case 2: return pixelValue<uint16_t>(address);
    case 4: return pixelValue<uint32_t>(address);
    case 8: return pixelValue<uint64_t>(address);
    default: return 0;
    }
}

/** @brief Read a pixel as a signed integer of its declared width. */
static int64_t readSignedPixel(const uint8_t* address, int32_t pixelBytes)
{
    switch (pixelBytes) {
    case 1: return pixelValue<int8_t>(address);
    case 2: return pixelValue<int16_t>(address);
    case 4: return pixelValue<int32_t>(address);
    case 8: return pixelValue<int64_t>(address);
    default: return 0;
    }
}

/** @brief Determine whether a raster pixel satisfies the requested condition. */
static bool isObject(const uint8_t* address, int32_t pixelBytes, const EdtCondition& condition)
{
    if (condition.op == EdtCondition::Operator::SIGNED_LESS
        || condition.op == EdtCondition::Operator::SIGNED_GREATER) {
        const int64_t value = readSignedPixel(address, pixelBytes);
        return condition.op == EdtCondition::Operator::SIGNED_LESS
            ? value < condition.signedValue : value > condition.signedValue;
    }
    if (condition.op == EdtCondition::Operator::UNSIGNED_LESS
        || condition.op == EdtCondition::Operator::UNSIGNED_GREATER) {
        const uint64_t value = readUnsignedPixel(address, pixelBytes);
        return condition.op == EdtCondition::Operator::UNSIGNED_LESS
            ? value < condition.valueBits : value > condition.valueBits;
    }

    const uint64_t value = readUnsignedPixel(address, pixelBytes);
    return condition.op == EdtCondition::Operator::EQUAL
        ? value == condition.valueBits : value != condition.valueBits;
}

/** @brief Compute one-dimensional squared distances using a parabola envelope. */
static void distanceTransform1D(
    const float* input,
    int32_t length,
    float* output,
    std::vector<int32_t>& sites,
    std::vector<float>& boundaries)
{
    int32_t lastSite = -1;
    for (int32_t q = 0; q < length; ++q) {
        if (!std::isfinite(input[q])) {
            continue;
        }

        float intersection = 0.0f;
        while (lastSite >= 0) {
            const int32_t previous = sites[lastSite];
            const float qFloat = static_cast<float>(q);
            const float previousFloat = static_cast<float>(previous);
            intersection = ((input[q] + qFloat * qFloat)
                - (input[previous] + previousFloat * previousFloat))
                / (2.0f * (qFloat - previousFloat));
            if (intersection > boundaries[lastSite]) {
                break;
            }
            --lastSite;
        }

        ++lastSite;
        sites[lastSite] = q;
        boundaries[lastSite] = lastSite == 0
            ? -std::numeric_limits<float>::infinity() : intersection;
        boundaries[lastSite + 1] = std::numeric_limits<float>::infinity();
    }

    if (lastSite < 0) {
        std::fill(output, output + length, std::numeric_limits<float>::infinity());
        return;
    }

    int32_t siteIndex = 0;
    for (int32_t q = 0; q < length; ++q) {
        while (siteIndex < lastSite && boundaries[siteIndex + 1] < static_cast<float>(q)) {
            ++siteIndex;
        }
        const float delta = static_cast<float>(q - sites[siteIndex]);
        output[q] = delta * delta + input[sites[siteIndex]];
    }
}

/** @brief Read and validate an optional half-open processing rectangle. */
static bool readBbox(
    const BinJSON::Any& command,
    int32_t width,
    int32_t height,
    int32_t& beginX,
    int32_t& beginY,
    int32_t& endX,
    int32_t& endY)
{
    beginX = 0;
    beginY = 0;
    endX = width;
    endY = height;
    const auto& bbox = command->get("bbox");
    if (!bbox || bbox->isNull()) {
        return true;
    }

    const auto& begin = bbox->get("begin");
    const auto& end = bbox->get("end");
    int64_t values[4];
    if (!begin || !end
        || !readInteger(begin->get("x"), values[0])
        || !readInteger(begin->get("y"), values[1])
        || !readInteger(end->get("x"), values[2])
        || !readInteger(end->get("y"), values[3])) {
        return false;
    }
    if (values[0] < 0 || values[1] < 0 || values[2] > width || values[3] > height
        || values[0] >= values[2] || values[1] >= values[3]) {
        return false;
    }
    beginX = static_cast<int32_t>(values[0]);
    beginY = static_cast<int32_t>(values[1]);
    endX = static_cast<int32_t>(values[2]);
    endY = static_cast<int32_t>(values[3]);
    return true;
}

} // namespace

/** @brief Return a raster of Euclidean distances to the nearest matching pixel. */
static BinJSON::Any edt(BinJSON::Any command)
{
    const auto& bitmapValue = command->get("bitmap");
    if (!bitmapValue || bitmapValue->getType() != BinJSON::BinJSONType::BINARY) {
        return BinJSON::makeError("EDT bitmap must be a raster binary buffer");
    }
    Buffer<> bitmap = bitmapValue->buffer();
    if (bitmap.size() < static_cast<int32_t>(offsetof(Raster<uint8_t>, data))) {
        return BinJSON::makeError("EDT bitmap is smaller than its raster header");
    }

    auto* source = bitmap.raw<Raster<uint8_t>*>();
    const int32_t width = source->width;
    const int32_t height = source->height;
    const int32_t stride = source->stride;
    const int32_t pixelBytes = source->pixelBytes;
    if (width <= 0 || height <= 0 || (pixelBytes != 1 && pixelBytes != 2
        && pixelBytes != 4 && pixelBytes != 8)
        || stride < static_cast<int64_t>(width) * pixelBytes) {
        return BinJSON::makeError("EDT bitmap has invalid raster dimensions or pixel size");
    }
    const int64_t expectedSize = static_cast<int64_t>(offsetof(Raster<uint8_t>, data))
        + static_cast<int64_t>(stride) * height;
    if (expectedSize > bitmap.size()) {
        return BinJSON::makeError("EDT bitmap data is truncated");
    }

    EdtCondition condition{};
    if (!readCondition(command, pixelBytes, condition)) {
        return BinJSON::makeError("EDT objectCondition or objectValue is invalid");
    }

    int32_t beginX;
    int32_t beginY;
    int32_t endX;
    int32_t endY;
    if (!readBbox(command, width, height, beginX, beginY, endX, endY)) {
        return BinJSON::makeError("EDT bbox is invalid");
    }

    const int32_t outputWidth = endX - beginX;
    const int32_t outputHeight = endY - beginY;
    const int64_t outputRowBytes = static_cast<int64_t>(outputWidth) * sizeof(float);
    const int64_t outputStride = ((outputRowBytes + 7) / 8) * 8;
    const int64_t outputSize = offsetof(Raster<float>, data) + outputStride * outputHeight;
    if (outputRowBytes > INT32_MAX - 7 || outputSize > RawBuffer::maximumCapacity) {
        return BinJSON::makeError("EDT output raster is too large");
    }
    auto distanceRaster = Raster<float>::create(outputWidth, outputHeight);
    auto* destination = reinterpret_cast<uint8_t*>(distanceRaster->data);
    const uint8_t* const sourceData = reinterpret_cast<const uint8_t*>(source->data);
    const float infinity = std::numeric_limits<float>::infinity();
    const int32_t maximumDimension = outputWidth > outputHeight ? outputWidth : outputHeight;
    std::vector<float> input(maximumDimension);
    std::vector<float> transformed(maximumDimension);
    std::vector<int32_t> sites(maximumDimension);
    std::vector<float> boundaries(maximumDimension + 1);

    for (int32_t y = beginY; y < endY; ++y) {
        const uint8_t* const sourceRow = sourceData + y * stride;
        for (int32_t x = beginX; x < endX; ++x) {
            const uint8_t* const pixel = sourceRow + x * pixelBytes;
            input[x - beginX] = isObject(pixel, pixelBytes, condition) ? 0.0f : infinity;
        }
        distanceTransform1D(input.data(), outputWidth, transformed.data(), sites, boundaries);
        auto* const outputRow = reinterpret_cast<float*>(destination)
            + (y - beginY) * (distanceRaster->stride / static_cast<int32_t>(sizeof(float)));
        std::memcpy(outputRow, transformed.data(), static_cast<int32_t>(outputWidth * sizeof(float)));
    }

    for (int32_t x = 0; x < outputWidth; ++x) {
        for (int32_t y = 0; y < outputHeight; ++y) {
            const auto* const row = reinterpret_cast<const float*>(destination)
                + y * (distanceRaster->stride / static_cast<int32_t>(sizeof(float)));
            input[y] = row[x];
        }
        distanceTransform1D(input.data(), outputHeight, transformed.data(), sites, boundaries);
        for (int32_t y = 0; y < outputHeight; ++y) {
            auto* const row = reinterpret_cast<float*>(destination)
                + y * (distanceRaster->stride / static_cast<int32_t>(sizeof(float)));
            row[x] = std::sqrt(transformed[y]);
        }
    }

    return binJSONObject({ { "distance", binJSON(distanceRaster) } });
}

REGISTER_COMMAND(edt);
