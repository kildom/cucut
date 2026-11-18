#include "../commands.hpp"
#include "../raster.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

/** @brief Return a label pixel using raster stride rather than packed row assumptions. */
static int16_t& pixelAt(LabelRaster* raster, int32_t x, int32_t y)
{
    auto* row = reinterpret_cast<int16_t*>(
        reinterpret_cast<uint8_t*>(raster->data) + y * raster->stride);
    return row[x];
}

/** @brief Validate metadata and collapse each positive component into adjacent negative regions. */
static BinJSON::Any collapseObjects(BinJSON::Any command)
{
    const auto& labeledValue = command->get("labeled");
    if (!labeledValue || labeledValue->getType() != BinJSON::BinJSONType::BINARY) {
        return BinJSON::makeError("CollapseObjects labeled must be a raster binary buffer");
    }
    Buffer<> labeledBuffer = labeledValue->buffer();
    if (labeledBuffer.size() < static_cast<int32_t>(offsetof(Raster<int16_t>, data))) {
        return BinJSON::makeError("CollapseObjects labeled raster is smaller than its header");
    }

    auto* raster = labeledBuffer.raw<LabelRaster*>();
    const int32_t width = raster->width;
    const int32_t height = raster->height;
    const int32_t stride = raster->stride;
    if (width <= 0 || height <= 0 || raster->pixelBytes != static_cast<int32_t>(sizeof(int16_t))
        || stride < static_cast<int64_t>(width) * static_cast<int64_t>(sizeof(int16_t))
        || stride % static_cast<int32_t>(alignof(int16_t)) != 0) {
        return BinJSON::makeError("CollapseObjects labeled raster has invalid dimensions or pixel size");
    }
    const int64_t expectedSize = static_cast<int64_t>(offsetof(Raster<int16_t>, data))
        + static_cast<int64_t>(stride) * height;
    if (expectedSize > labeledBuffer.size()) {
        return BinJSON::makeError("CollapseObjects labeled raster data is truncated");
    }

    const auto& info = command->get("info");
    if (!info || info->getType() != BinJSON::BinJSONType::ARRAY) {
        return BinJSON::makeError("CollapseObjects info must be an array");
    }
    const int32_t labelCount = info->length();
    for (int32_t label = 0; label < labelCount; ++label) {
        const auto& item = info->get(label);
        const auto& index = item ? item->get("index") : BinJSON::Any{};
        if (!item || item->getType() != BinJSON::BinJSONType::OBJECT
            || !index || index->getType() != BinJSON::BinJSONType::NUMBER
            || index->number() != label) {
            return BinJSON::makeError("CollapseObjects info indices must match their array positions");
        }
    }

    const int64_t pixelCount64 = static_cast<int64_t>(width) * height;
    if (pixelCount64 > INT32_MAX) {
        return BinJSON::makeError("CollapseObjects labeled raster is too large");
    }
    const int32_t pixelCount = static_cast<int32_t>(pixelCount64);
    std::vector<int32_t> starts(labelCount, -1);
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            const int16_t label = pixelAt(raster, x, y);
            if (label == INT16_MIN || (label >= 0 && label >= labelCount)) {
                return BinJSON::makeError("CollapseObjects raster contains an invalid label");
            }
            if (label >= 0 && starts[label] < 0) {
                starts[label] = y * width + x;
            }
        }
    }

    std::vector<uint8_t> visited(static_cast<std::size_t>(pixelCount), 0);
    std::vector<int32_t> component;

    for (int32_t label = 0; label < labelCount; ++label) {
        if (starts[label] < 0) {
            return BinJSON::makeError("CollapseObjects info contains a label missing from the raster");
        }

        component.clear();
        component.push_back(starts[label]);
        visited[static_cast<std::size_t>(starts[label])] = 1;
        for (int32_t position = 0; position < static_cast<int32_t>(component.size()); ++position) {
            const int32_t current = component[position];
            const int32_t x = current % width;
            const int32_t y = current / width;
            constexpr int32_t dx4[4] = { -1, 1, 0, 0 };
            constexpr int32_t dy4[4] = { 0, 0, -1, 1 };
            for (int32_t direction = 0; direction < 4; ++direction) {
                const int32_t nextX = x + dx4[direction];
                const int32_t nextY = y + dy4[direction];
                if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height) {
                    continue;
                }
                const int32_t next = nextY * width + nextX;
                if (!visited[static_cast<std::size_t>(next)]
                    && pixelAt(raster, nextX, nextY) == label) {
                    visited[static_cast<std::size_t>(next)] = 1;
                    component.push_back(next);
                }
            }
        }

        const int32_t objectPixelCount = static_cast<int32_t>(component.size());
        int32_t seedCount = 0;
        for (int32_t position = 0; position < objectPixelCount; ++position) {
            const int32_t current = component[position];
            const int32_t x = current % width;
            const int32_t y = current / width;
            int16_t adjacentLabel = INT16_MAX;
            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const int32_t nextX = x + dx;
                    const int32_t nextY = y + dy;
                    if (nextX >= 0 && nextX < width && nextY >= 0 && nextY < height) {
                        const int32_t neighborIndex = nextY * width + nextX;
                        const int16_t neighbor = pixelAt(raster, nextX, nextY);
                        if (neighbor < 0 && !visited[static_cast<std::size_t>(neighborIndex)]
                            && neighbor < adjacentLabel) {
                            adjacentLabel = neighbor;
                        }
                    }
                }
            }
            if (adjacentLabel != INT16_MAX) {
                pixelAt(raster, x, y) = adjacentLabel;
                component[seedCount++] = current;
            }
        }
        if (seedCount == 0) {
            return BinJSON::makeError("CollapseObjects label has no adjacent background or hole");
        }
        component.resize(seedCount);
        std::sort(component.begin(), component.end(), [raster, width](int32_t left, int32_t right) {
            const int16_t leftLabel = pixelAt(raster, left % width, left / width);
            const int16_t rightLabel = pixelAt(raster, right % width, right / width);
            return leftLabel != rightLabel ? leftLabel < rightLabel : left < right;
        });

        int32_t cursor = 0;
        while (cursor < static_cast<int32_t>(component.size())) {
            const int32_t current = component[cursor++];
            const int32_t x = current % width;
            const int32_t y = current / width;
            const int16_t sourceLabel = pixelAt(raster, x, y);
            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const int32_t nextX = x + dx;
                    const int32_t nextY = y + dy;
                    if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height
                        || pixelAt(raster, nextX, nextY) != label) {
                        continue;
                    }
                    pixelAt(raster, nextX, nextY) = sourceLabel;
                    component.push_back(nextY * width + nextX);
                }
            }
        }
        if (static_cast<int32_t>(component.size()) != objectPixelCount) {
            return BinJSON::makeError("CollapseObjects could not reach every pixel in a label");
        }
    }

    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            if (pixelAt(raster, x, y) >= 0) {
                return BinJSON::makeError("CollapseObjects could not collapse every labeled pixel");
            }
        }
    }

    return binJSONObject({ { "collapsed", binJSON(labeledBuffer) } });
}

} // namespace

REGISTER_COMMAND(collapseObjects);
