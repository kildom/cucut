#ifndef CUCUT_RAWBUFFER_HPP
#define CUCUT_RAWBUFFER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#include "errors.hpp"
#include "ref.hpp"

/** @brief Reference-counted, resizable byte buffers accessed through pointers. */
namespace RawBuffer {

inline constexpr int32_t alignment = 64;
inline constexpr int32_t maximumCapacity = 1024 * 1024 * 1024;

namespace prv {

struct ControlBlock
{
    void* allocatedPointer;
    int32_t _reserved;
    RefCounter refs;
    int32_t allocatedSize;
    int32_t size;
    uint8_t buffer[1];
};

constexpr int32_t bufferOffset = static_cast<int32_t>(offsetof(ControlBlock, buffer));

static ControlBlock* get(const void* buffer);
static ControlBlock* allocate(int32_t newSize, int32_t preallocateSize);
static void decrement(ControlBlock* cb);

static int32_t requestedCapacity(int32_t size, int32_t preallocateSize)
{
    CUCUT_ASSERT(size >= 0, "Size cannot be negative. It is %d.", size);
    CUCUT_ASSERT(preallocateSize >= 0, "Preallocation size cannot be negative. It is %d.", preallocateSize);

    if (preallocateSize > maximumCapacity
        || size > maximumCapacity - preallocateSize) {
        fatalError("Requested Buffer allocation is too large.");
    }
    return size + preallocateSize;
}

static int32_t requiredAllocationSize(int32_t capacity)
{
    return bufferOffset + capacity + alignment - 1;
}

static ControlBlock* allocate(int32_t newSize, int32_t preallocateSize)
{
    auto requiredSize = requiredAllocationSize(requestedCapacity(newSize, preallocateSize));
    auto* ptr = static_cast<uint8_t*>(std::malloc(requiredSize));
    if (!ptr) {
        fatalError("Out of memory when allocating buffer.");
    }

    uintptr_t cbAddress = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t bufferAddress = cbAddress + bufferOffset;
    int32_t allocatedSize = requiredSize - bufferOffset;
    int32_t misalignment = static_cast<int32_t>(bufferAddress & (alignment - 1));
    if (misalignment != 0) {
        int32_t adjustment = alignment - misalignment;
        cbAddress += adjustment;
        allocatedSize -= adjustment;
    }

    auto* cb = ::new (reinterpret_cast<void*>(cbAddress)) ControlBlock;
    cb->allocatedPointer = ptr;
    cb->allocatedSize = allocatedSize;
    cb->size = newSize;
    return cb;
}

static ControlBlock* reallocate(ControlBlock* cbOld, int32_t newSize, bool copyContent)
{
    CUCUT_ASSERT(cbOld != nullptr, "NULL pointer access");
    CUCUT_ASSERT(cbOld->refs.valid(), "Operation on reallocated or corrupted memory");
    CUCUT_ASSERT(newSize >= 0, "Size cannot be negative. It is %d.", newSize);
    if (newSize > maximumCapacity) {
        fatalError("Requested Buffer allocation is too large.");
    }

    auto requiredSize = requiredAllocationSize(newSize);
    auto* ptrOld = static_cast<uint8_t*>(cbOld->allocatedPointer);
    int32_t oldAdjustment = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(cbOld) - reinterpret_cast<uintptr_t>(ptrOld));
    int32_t oldSize = cbOld->size;
    auto* ptr = static_cast<uint8_t*>(std::realloc(ptrOld, requiredSize));
    if (!ptr) {
        fatalError("Out of memory when allocating buffer.");
    } else if (ptr == ptrOld) {
        cbOld->allocatedSize = requiredSize - bufferOffset - oldAdjustment;
        return cbOld;
    }

    uintptr_t bufferAddress = reinterpret_cast<uintptr_t>(ptr) + bufferOffset;
    int32_t misalignment = static_cast<int32_t>(bufferAddress & (alignment - 1));
    int32_t newAdjustment = misalignment == 0 ? 0 : alignment - misalignment;
    auto* cbNew = reinterpret_cast<ControlBlock*>(ptr + newAdjustment);

    if (newAdjustment != oldAdjustment) {
        int32_t copyBytes = copyContent ? std::min(oldSize, newSize) : 0;
        std::memmove(cbNew, ptr + oldAdjustment, bufferOffset + copyBytes);
    }

    cbNew->allocatedPointer = ptr;
    cbNew->allocatedSize = requiredSize - bufferOffset - newAdjustment;
    return cbNew;
}

static ControlBlock* get(const void* buffer)
{
    CUCUT_ASSERT(buffer != nullptr, "NULL pointer access");
    uintptr_t bufferAddress = reinterpret_cast<uintptr_t>(buffer);
    CUCUT_ASSERT(bufferAddress >= bufferOffset, "Invalid Buffer pointer.");
    auto* cb = reinterpret_cast<ControlBlock*>(bufferAddress - bufferOffset);
    CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");
    return cb;
}

static void free(ControlBlock* cb)
{
    CUCUT_ASSERT(cb != nullptr, "NULL pointer access");
    CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");
    void* allocatedPointer = cb->allocatedPointer;
    cb->~ControlBlock();
    std::free(allocatedPointer);
}

static ControlBlock* resize(
    ControlBlock* cb,
    int32_t newSize,
    int32_t preallocateSize,
    bool grow,
    bool copyContent)
{
    CUCUT_ASSERT(newSize >= 0, "Size cannot be negative. It is %d.", newSize);
    CUCUT_ASSERT(preallocateSize >= 0, "Preallocation size cannot be negative. It is %d.", preallocateSize);
    int32_t requestedSize = requestedCapacity(newSize, preallocateSize);
    CUCUT_ASSERT(cb != nullptr, "NULL pointer access");
    CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");

    if (cb->size == newSize) {
        return cb;
    }
    if (cb->refs.get() != 1) {
        auto* cbNew = allocate(newSize, preallocateSize);
        if (copyContent) {
            int32_t copySize = std::min(newSize, cb->size);
            std::memcpy(cbNew->buffer, cb->buffer, copySize);
        }
        decrement(cb);
        return cbNew;
    }

    if (newSize <= cb->allocatedSize) {
        int32_t excessCapacity = cb->allocatedSize - newSize;
        int32_t shrinkThreshold = 2 * alignment + preallocateSize;
        if (grow || excessCapacity <= shrinkThreshold) {
            cb->size = newSize;
            return cb;
        }
    }

    auto* cbNew = reallocate(cb, requestedSize, copyContent);
    cbNew->size = newSize;
    return cbNew;
}

static void increment(ControlBlock* cb)
{
    CUCUT_ASSERT(cb != nullptr, "NULL pointer access");
    CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");
    cb->refs.inc();
}

static void decrement(ControlBlock* cb)
{
    CUCUT_ASSERT(cb != nullptr, "NULL pointer access");
    CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");
    if (!cb->refs.dec()) {
        free(cb);
    }
}

static_assert(offsetof(ControlBlock, buffer) == bufferOffset,
    "ControlBlock members must remain packed before the buffer");
static_assert(offsetof(ControlBlock, buffer) % alignof(ControlBlock) == 0,
    "Aligning buffer must also align ControlBlock");

}

/** @brief Allocate a buffer with the requested size and extra capacity.
 *
 * Both sizes must be non-negative. The returned buffer has one reference.
 */
static inline uint8_t* alloc(int32_t newSize, int32_t preallocateSize)
{
    return prv::allocate(newSize, preallocateSize)->buffer;
}

/** @brief Resize a buffer, reallocating only when its capacity requires it.
 *
 * Sizes must be non-negative. @p grow controls whether excess capacity is
 * retained when shrinking, and @p copyContent controls data preservation if
 * a new allocation is needed.
 */
static inline uint8_t* realloc(
    uint8_t* buffer,
    int32_t newSize,
    int32_t preallocateSize,
    bool grow,
    bool copyContent)
{
    return prv::resize(
        prv::get(buffer), newSize, preallocateSize, grow, copyContent)->buffer;
}

/** @brief Add one reference to a buffer. */
static inline void inc(uint8_t* buffer)
{
    prv::increment(prv::get(buffer));
}

/** @brief Release one reference and free the buffer when it was the last. */
static inline void dec(uint8_t* buffer)
{
    prv::decrement(prv::get(buffer));
}

/** @brief Ensure the caller is the sole owner, cloning the buffer if shared.
 *
 * The returned pointer must be used after this call; when shared, the passed
 * pointer loses one reference and the returned clone has one reference.
 */
static inline uint8_t* unique(uint8_t* buffer)
{
    auto* cb = prv::get(buffer);
    if (cb->refs.get() == 1) {
        return buffer;
    }

    auto* cbNew = prv::allocate(cb->size, 0);
    std::memcpy(cbNew->buffer, cb->buffer, cb->size);
    prv::decrement(cb);
    return cbNew->buffer;
}

/** @brief Return the logical size of a live buffer in bytes. */
static inline int32_t size(const uint8_t* buffer)
{
    return prv::get(buffer)->size;
}

/** @brief Return the allocated data capacity of a live buffer in bytes. */
static inline int32_t allocatedSize(const uint8_t* buffer)
{
    return prv::get(buffer)->allocatedSize;
}

/** @brief Return the number of references to a buffer. */
static inline int32_t refs(const uint8_t* buffer)
{
    return (int32_t)prv::get(buffer)->refs.get();
}

}

#endif // CUCUT_RAWBUFFER_HPP
