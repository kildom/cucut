

#include "wasm.hpp"
#include "buffer.hpp"


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
