#include <cstdint>

#include "rawbuffer.hpp"
#include "wasm.hpp"

WASM_EXPORT(alloc)
uint8_t* RawBufferAlloc(int32_t newSize, int32_t preallocateSize)
{
    return RawBuffer::alloc(newSize, preallocateSize);
}

WASM_EXPORT(realloc)
uint8_t* RawBufferRealloc(
    uint8_t* buffer,
    int32_t newSize,
    int32_t preallocateSize,
    int32_t grow,
    int32_t copyContent)
{
    return RawBuffer::realloc(
        buffer, newSize, preallocateSize, grow != 0, copyContent != 0);
}

WASM_EXPORT(inc)
void RawBufferInc(uint8_t* buffer)
{
    RawBuffer::inc(buffer);
}

WASM_EXPORT(dec)
void RawBufferDec(uint8_t* buffer)
{
    RawBuffer::dec(buffer);
}

WASM_EXPORT(unique)
uint8_t* RawBufferUnique(uint8_t* buffer)
{
    return RawBuffer::unique(buffer);
}

WASM_EXPORT(refs)
int32_t RawBufferRefs(const uint8_t* buffer)
{
    return RawBuffer::refs(buffer);
}
