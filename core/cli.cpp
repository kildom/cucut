
#if !defined(__wasm__) && !defined(__wasm32__) && !defined(__wasm64__)

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "buffer.hpp"
#include "rawbuffer.hpp"

extern "C" uint8_t* request(uint8_t* inputBufferPtr);

namespace {

bool readFile(const std::string& path, Buffer<>& buffer)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Cannot open input file: " << path << '\n';
        return false;
    }

    const std::streamoff fileSize = file.tellg();
    if (fileSize < 0 || fileSize > RawBuffer::maximumCapacity) {
        std::cerr << "Input file has an invalid or unsupported size: " << path << '\n';
        return false;
    }

    buffer.size(static_cast<int32_t>(fileSize), false);
    file.seekg(0, std::ios::beg);
    if (fileSize > 0 && !file.read(
            reinterpret_cast<char*>(buffer.raw<uint8_t*>()),
            static_cast<std::streamsize>(fileSize))) {
        std::cerr << "Cannot read input file: " << path << '\n';
        return false;
    }
    return true;
}

bool writeFile(const std::string& path, const Buffer<>& buffer)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        std::cerr << "Cannot open output file: " << path << '\n';
        return false;
    }
    if (buffer.size() > 0 && !file.write(
            reinterpret_cast<const char*>(buffer.raw<uint8_t*>()),
            static_cast<std::streamsize>(buffer.size()))) {
        std::cerr << "Cannot write output file: " << path << '\n';
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: cucut-cli <output-stem> <input-0> [input-1 ...]\n";
        return 2;
    }

    std::vector<Buffer<>> inputBuffers;
    inputBuffers.reserve(static_cast<size_t>(argc - 2));
    for (int i = 2; i < argc; ++i) {
        Buffer<> buffer;
        if (!readFile(argv[i], buffer)) {
            return 1;
        }
        inputBuffers.emplace_back(std::move(buffer));
    }

    Buffer<void*> inputArray(static_cast<int32_t>(
        inputBuffers.size() * sizeof(void*)));
    auto** inputPointers = inputArray.raw<void**>();
    for (int32_t i = 0; i < static_cast<int32_t>(inputBuffers.size()); ++i) {
        inputPointers[i] = inputBuffers[i].raw<void*>();
    }

    uint8_t* rawOutputArray = request(inputArray.raw<uint8_t*>());
    if (rawOutputArray == nullptr) {
        std::cerr << "Core request failed to return an output buffer array.\n";
        return 1;
    }

    Buffer<void*> outputArray(rawOutputArray);
    RawBuffer::dec(rawOutputArray);
    if (outputArray.size() < static_cast<int32_t>(sizeof(void*))
        || outputArray.size() % static_cast<int32_t>(sizeof(void*)) != 0) {
        std::cerr << "Core request returned an invalid output buffer array.\n";
        return 1;
    }

    const int32_t outputCount = outputArray.size()
        / static_cast<int32_t>(sizeof(void*));
    auto** outputPointers = outputArray.raw<void**>();
    for (int32_t i = 0; i < outputCount; ++i) {
        if (outputPointers[i] == nullptr) {
            std::cerr << "Core request returned a null output buffer.\n";
            return 1;
        }

        auto* outputPointer = static_cast<uint8_t*>(outputPointers[i]);
        Buffer<> outputBuffer(outputPointer);
        RawBuffer::dec(outputPointer);
        const std::string outputPath = std::string(argv[1]) + "-"
            + std::to_string(i) + ".raw";
        if (!writeFile(outputPath, outputBuffer)) {
            return 1;
        }
    }

    return 0;
}

#endif
