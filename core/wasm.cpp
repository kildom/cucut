
#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)

#include <vector>

#include "wasm.hpp"
#include "buffer.hpp"
#include "binjson.hpp"


WASM_EXPORT(alloc)
uint8_t* alloc(int32_t newSize, int32_t prealloc) {
    return Buffer::Wasm::alloc(newSize, prealloc);
}

WASM_EXPORT(inc)
void inc(uint8_t* buffer) {
    Buffer::Wasm::inc(buffer);
}
WASM_EXPORT(dec)
void dec(uint8_t* buffer) {
    Buffer::Wasm::dec(buffer);
}

WASM_EXPORT(resize)
uint8_t* resize(uint8_t* buffer, int32_t newSize, int32_t preallocateSize, int32_t grow, int32_t copyContent) {
    return Buffer::Wasm::resize(buffer, newSize, preallocateSize, grow != 0, copyContent != 0);
}

WASM_EXPORT(request)
uint8_t* request(uint8_t* inputBufferPtr) {
    std::vector<Buffer> inputBuffers;
    Buffer inputBuffer(inputBufferPtr);
    auto item = inputBuffer.to<void**>();
    auto last = item + inputBuffer.size() / sizeof(void*);
    for (item = item; item != last; item++) {
        inputBuffers.emplace_back(*item);
    }
    auto requestBody = BinJSON::parse(inputBuffers);

    // TODO: Execute request
    auto responseBody = requestBody;

    std::vector<Buffer> outputBuffers;
    BinJSON::generate(responseBody, outputBuffers);
    Buffer outputBuffer(outputBuffers.size() * sizeof(void*));
    item = outputBuffer.to<void**>();
    for (int i = 0; i < outputBuffers.size(); i++) {
        item[i] = outputBuffers[i];
        inc((uint8_t*)item[i]);
    }
    uint8_t* result = outputBuffer;
    inc(result);
    return result;
}

#endif
