#include <vector>

#include "wasm.hpp"
#include "buffer.hpp"
#include "binjson.hpp"
#include "commands.hpp"

WASM_EXPORT(request)
uint8_t* request(uint8_t* inputBufferPtr)
{
    std::vector<Buffer<>> inputBuffers;
    Buffer<> inputBuffer(inputBufferPtr);
    auto* item = inputBuffer.raw<void**>();
    auto* last = item + inputBuffer.size() / static_cast<int32_t>(sizeof(void*));
    for (; item != last; ++item) {
        inputBuffers.emplace_back(*item);
    }

    auto requestBody = BinJSON::parse(inputBuffers);
    auto responseBody = requestBody->isError()
        ? requestBody
        : executeCommand(requestBody);

    std::vector<Buffer<>> outputBuffers;
    BinJSON::generate(responseBody, outputBuffers);
    Buffer<> outputBuffer(static_cast<int32_t>(outputBuffers.size() * sizeof(void*)));
    auto** outputPointers = outputBuffer.raw<void**>();
    for (int32_t i = 0; i < static_cast<int32_t>(outputBuffers.size()); ++i) {
        outputPointers[i] = outputBuffers[i].shareRaw<void*>();
    }
    return outputBuffer.shareRaw<uint8_t*>();
}
