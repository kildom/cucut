
//#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)

#include <vector>

#include "wasm.hpp"
#include "buffer.hpp"
#include "binjson.hpp"
#include "rawbuffer.hpp"


WASM_EXPORT(alloc)
uint8_t* alloc(int32_t newSize, int32_t prealloc) {
    return RawBuffer::alloc(newSize, prealloc);
}

WASM_EXPORT(inc)
void inc(uint8_t* buffer) {
    RawBuffer::inc(buffer);
}
WASM_EXPORT(dec)
void dec(uint8_t* buffer) {
    RawBuffer::dec(buffer);
}

WASM_EXPORT(resize)
uint8_t* resize(uint8_t* buffer, int32_t newSize, int32_t preallocateSize, int32_t grow, int32_t copyContent) {
    return RawBuffer::realloc(buffer, newSize, preallocateSize, grow != 0, copyContent != 0);
}

WASM_EXPORT(request)
uint8_t* request(uint8_t* inputBufferPtr) {
    std::vector<Buffer<>> inputBuffers;
    Buffer<> inputBuffer(inputBufferPtr);
    auto item = inputBuffer.raw<void**>();
    auto last = item + inputBuffer.size() / sizeof(void*);
    for (item = item; item != last; item++) {
        inputBuffers.emplace_back(*item);
    }
    auto requestBody = BinJSON::parse(inputBuffers);

    // TODO: Execute request
    auto responseBody = requestBody;

    std::vector<Buffer<>> outputBuffers;
    BinJSON::generate(responseBody, outputBuffers);
    Buffer<> outputBuffer(outputBuffers.size() * sizeof(void*));
    item = outputBuffer.raw<void**>();
    for (int i = 0; i < outputBuffers.size(); i++) {
        item[i] = outputBuffers[i].shareRaw<void*>();
    }
    return outputBuffer.shareRaw<uint8_t*>();
}

WASM_EXPORT(getStackPointer)
__attribute__((naked))
uint32_t getStackPointer() {
    __asm__ volatile ("global.get __stack_pointer\nreturn");
}


WASM_EXPORT(setStackPointer)
__attribute__((naked))
void setStackPointer(uint32_t value) {
    __asm__ volatile ("local.get 0\nglobal.set __stack_pointer\nreturn");
}


/** @brief Get number of threads available in thread pool including main thread.
 */
WASM_IMPORT(threadPoolSize)
uint32_t threadPoolSize();


/** @brief Get current thread index, 0 is the main thread.
 */
WASM_IMPORT(threadPoolCurrent)
uint32_t threadPoolCurrent();

struct ThreadPoolJob {
    void (*callback)(ThreadPoolJob* job);
};

WASM_IMPORT(threadPoolExecute)
void threadPoolExecute(ThreadPoolJob* job);

WASM_EXPORT(threadPoolEntry)
void threadPoolEntry(ThreadPoolJob* job)
{
    job->callback(job);
}


//#endif
