#ifndef CUCUT_BUFFER_HPP
#define CUCUT_BUFFER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

#include "ref.hpp"
#include "errors.hpp"

/** @brief Reference-counted, resizable byte buffer.
 *
 * A Buffer owns a reference to a 64-byte-aligned data area with a reference
 * count and allocation metadata immediately before it. Copying a Buffer shares
 * the allocation.
 * Constness applies only to the Buffer reference, not to the referenced
 * memory; pointer conversions always provide mutable access.
 *
 * Resizing a uniquely owned Buffer reallocates its allocation. Resizing a
 * shared Buffer creates a new allocation for the resized instance; other
 * references continue to point to the old allocation and do not observe the
 * new size or contents. This is intentional copy-on-resize behavior.
 *
 * The buffer is always allocated, including when its size is zero.
 */
class Buffer
{
private:

    /** @brief Control block immediately preceding a buffer's data.
     *
     * The data is aligned to 64 bytes. `allocatedPointer` retains the pointer
     * returned by `malloc`/`realloc`, which may differ from the aligned data
     * address.
     */
    struct ControlBlock
    {
        static constexpr int32_t alignment = 64;

        /** @brief Pointer passed to the allocator's deallocation/reallocation functions. */
        void* allocatedPointer;
        /** @brief Actual available byte capacity starting at the aligned data pointer. */
        int32_t allocatedSize;
        /** @brief Number of Buffer instances sharing this allocation. */
        RefCounter refs;
        /** @brief Reserved for future use. */
        int32_t _reserved;
        /** @brief Number of bytes in the buffer. */
        int32_t size;
        /** @brief Buffer data, aligned to 64 bytes. */
        uint8_t buffer[];

        /** @brief Allocate new buffer with control block.
         * 
         * The buffer has initially reference counter set to 1.
         */
        static ControlBlock* alloc(int32_t newSize, int32_t preallocateSize)
        {
            CUCUT_ASSERT(newSize >= 0, "Size cannot be negative. It is %d.", newSize);

            auto requiredSize = (int32_t)offsetof(ControlBlock, buffer) + newSize + alignment - 1 + preallocateSize;
            uint8_t* ptr = (uint8_t*)std::malloc(requiredSize);
            if (!ptr) {
                fatalError("Out of memory when allocating buffer.");
            }
            uintptr_t cbAddress = (uintptr_t)ptr;
            uintptr_t bufferAddress = cbAddress + offsetof(ControlBlock, buffer);
            int32_t allocatedSize = requiredSize - offsetof(ControlBlock, buffer);
            int32_t misalignment = bufferAddress & (alignment - 1);
            if (misalignment != 0) {
                cbAddress += alignment - misalignment;
                bufferAddress += alignment - misalignment;
                allocatedSize -= alignment - misalignment;
            }
            ControlBlock* cb = ::new ((ControlBlock*)cbAddress) ControlBlock;
            cb->allocatedPointer = ptr;
            cb->allocatedSize = allocatedSize;
            cb->size = newSize;
            return cb;
        }

        /** @brief Reallocate a buffer with control block.
         * 
         * The buffer with control block will be reallocated if needed.
         * The reference counter is unchanged.
         * The returned pointer may or may not be the same as cbOld.
         */
        static ControlBlock* realloc(ControlBlock* cbOld, int32_t newSize, bool copyContent)
        {
            CUCUT_ASSERT(cbOld != nullptr, "NULL pointer access");
            CUCUT_ASSERT(newSize >= 0, "Size cannot be negative. It is %d.", newSize);
            CUCUT_ASSERT(cbOld->refs.valid(), "Operation on reallocated or corrupted memory");

            auto requiredSize = (int32_t)offsetof(ControlBlock, buffer) + newSize + alignment - 1;
            uint8_t* ptrOld = (uint8_t*)cbOld->allocatedPointer;
            uint8_t* ptr = (uint8_t*)std::realloc(ptrOld, requiredSize);
            if (!ptr) {
                // No memory - fatal error
                fatalError("Out of memory when allocating buffer.");
            } else if (ptr == ptrOld) {
                // The same pointer - just update fields
                cbOld->allocatedSize = requiredSize - offsetof(ControlBlock, buffer);
                cbOld->size = newSize;
                return cbOld;
            }

            int32_t oldAdjustment = (uintptr_t)cbOld - (uintptr_t)cbOld->allocatedPointer;
            uintptr_t cbAddress = (uintptr_t)ptr;
            uintptr_t bufferAddress = cbAddress + offsetof(ControlBlock, buffer);
            int32_t misalignment = bufferAddress & (alignment - 1);
            int32_t newAdjustment = 0;
            if (misalignment != 0) {
                newAdjustment = alignment - misalignment;
            }

            ControlBlock* cbNew = (ControlBlock*)((uint8_t*)ptr + newAdjustment);

            if (newAdjustment != oldAdjustment) {
                // Different adjustment - calculate old location and do memmove
                ControlBlock* cbOld = (ControlBlock*)((uint8_t*)ptr + oldAdjustment);
                int32_t copyBytes = copyContent
                    ? std::min(cbOld->size, newSize)
                    : 0;
                std::memmove(cbNew, cbOld, offsetof(ControlBlock, buffer) + copyBytes);
            }

            cbNew->allocatedPointer = ptr;
            cbNew->allocatedSize = requiredSize - offsetof(ControlBlock, buffer) - newAdjustment;
            cbNew->size = newSize;
            return cbNew;
        }

        /** Get control block from associated buffer pointer.
         */
        static ControlBlock* get(void* buffer)
        {
            CUCUT_ASSERT(buffer != nullptr, "NULL pointer access");
            auto cb = reinterpret_cast<ControlBlock*>(buffer - offsetof(ControlBlock, buffer));
            CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");
            return cb;
        }

        /** Deallocates buffer and control block.
         */
        static void free(ControlBlock* cb)
        {
            CUCUT_ASSERT(cb != nullptr, "NULL pointer access");
            CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");

            cb->~ControlBlock();
            std::free(cb->allocatedPointer);
        }

        /** @brief Resize existing buffer.
         * 
         * Reference counter is handled by this function. If new buffer is allocated, it will have ref counter 1
         * and old buffer will have its ref counter decremented. If no new allocation was done, no ref counter is
         * modified.
         * 
         * The functions ensures that it is only owner before modifing the control block.
         * 
         * @param cb              Old control block to resize. After call, this pointer cannot be used any more.
         * @param newSize         New size of the buffer.
         * @param preallocateSize Number of bytes to preallocate after requested memory to allow faster buffer grow.
         * @param grow            Avoid reducing size forcing allocated memory to only grow.
         * @param copyContent     Content should be copied into the new buffer if memory is reallocated.
         * @returns               Control block of resized buffer, can be the same as cb or different.
         */
        static ControlBlock* resize(ControlBlock* cb, int32_t newSize, int32_t preallocateSize, bool grow, bool copyContent)
        {
            CUCUT_ASSERT(newSize >= 0, "Invalid size %d.", newSize);
            CUCUT_ASSERT(preallocateSize >= 0, "Invalid preallocate size %d.", newSize);
            CUCUT_ASSERT(cb != nullptr, "NULL pointer access");
            CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");

            if (cb->size == newSize) {
                return cb;
            } else if (cb->refs.get() != 1) {
                auto cbNew = alloc(newSize, preallocateSize);
                if (copyContent) {
                    int32_t copySize = std::min(newSize, cb->size);
                    std::memcpy(cbNew->buffer, cb->buffer, copySize);
                }
                dec(cb);
                return cbNew;
            }

            if (newSize <= cb->allocatedSize) {
                int32_t rangeMin = grow
                    ? 0
                    : cb->allocatedSize - 2 * alignment - preallocateSize;

                if (rangeMin <= newSize) {
                    cb->size = newSize;
                    return cb;
                }
            }

            auto cbNew = realloc(cb, newSize + preallocateSize, copyContent);
            cbNew->size = newSize;

            return cbNew;
        }

        static void inc(ControlBlock* cb)
        {
            CUCUT_ASSERT(cb != nullptr, "NULL pointer access");
            CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");
            cb->refs.inc();
        }

        static void dec(ControlBlock* cb)
        {
            CUCUT_ASSERT(cb != nullptr, "NULL pointer access");
            CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");
            if (!cb->refs.dec()) {
                free(cb);
            }
        }

    };


    uint8_t* buffer;

public:
    /** @brief Construct an allocated, empty buffer. */
    Buffer() : buffer(ControlBlock::alloc(0, 0)->buffer) {}

    /** @brief Construct a buffer with the requested size.
     * @param size Number of bytes to allocate.
     */
    explicit Buffer(int32_t size, int32_t prealloc) : buffer(ControlBlock::alloc(size, prealloc)->buffer) {}

    /** @brief Copy a reference to the same allocation. */
    Buffer(const Buffer& other) : buffer(other.buffer)
    {
        ControlBlock::inc(ControlBlock::get(other.buffer));
    }

    /** @brief Transfer a reference, leaving the source empty and valid. */
    Buffer(Buffer&& other) noexcept : buffer(other.buffer)
    {
        other.buffer = nullptr;
    }

    Buffer(void* ptr): buffer((uint8_t*)ptr)
    {
        ControlBlock::inc(ControlBlock::get(buffer));
    }

    /** @brief Release this reference. */
    ~Buffer()
    {
        if (buffer) {
            ControlBlock::dec(ControlBlock::get(buffer));
        }
    }

    /** @brief Share the allocation held by another Buffer. */
    Buffer& operator=(const Buffer& other)
    {
        ControlBlock::inc(ControlBlock::get(other.buffer));
        if (buffer) {
            ControlBlock::dec(ControlBlock::get(buffer));
        }
        buffer = other.buffer;
        return *this;
    }

    /** @brief Transfer the allocation held by another Buffer. */
    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other)
        {
            if (buffer) {
                ControlBlock::dec(ControlBlock::get(buffer));
            }
            buffer = other.buffer;
            other.buffer = nullptr;
        }
        return *this;
    }

    /** @brief Exchange allocations with another Buffer. */
    void swap(Buffer& other) noexcept
    {
        std::swap(buffer, other.buffer);
    }

    /** @brief Return the current number of bytes. */
    uint32_t size() const
    {
        CUCUT_ASSERT(buffer, "Null pointer operation!");
        return ControlBlock::get(buffer)->size;
    }

    /** @brief Ensure the buffer has exactly the requested size.
     *
     * A unique allocation is reused when its capacity is sufficient and
     * reallocated otherwise. A shared allocation is detached whenever its
     * logical size would change, leaving its other references and metadata
     * unchanged. When preserving content, the first min(old size, new size)
     * bytes are retained.
     *
     * @param newSize Requested number of bytes.
     * @param keepContent Whether to preserve existing content; true by default.
     * @param preallocateSize Extra capacity requested if this call allocates.
     * Ignored when the current allocation already has enough capacity.
     * A shared buffer is detached before its logical size changes.
     * @throws std::length_error If requested size plus preallocation overflows.
     */
    void size(int32_t newSize, bool keepContent = true, int32_t preallocateSize = 0, bool growOnly = false)
    {
        auto cb = ControlBlock::get(buffer);
        auto cbNew = ControlBlock::resize(cb, newSize, preallocateSize, growOnly, keepContent);
        buffer = cbNew->buffer;
    }

    /** @brief Increase the buffer size when needed, reserving room for later growth.
     *
     * Does nothing when @p newSize is no greater than the current size.
     * Otherwise requests the exact logical size and, if allocation is needed,
     *
     * @param newSize Exact requested logical size.
     * @param keepContent Whether to preserve existing content; true by default.
     * @throws std::length_error If the requested capacity exceeds uint32_t.
     */
    void grow(int32_t newSize, bool keepContent = true)
    {
        auto cb = ControlBlock::get(buffer);
        auto cbNew = ControlBlock::resize(cb, newSize, 64 + newSize / 4, true, keepContent);
        buffer = cbNew->buffer;
    }

    /** @brief Return the data pointer as signed 8-bit values.
     * @note A const Buffer reference still exposes mutable buffer contents.
     */
    operator int8_t*() const { return reinterpret_cast<int8_t*>(buffer); }
    /** @brief Return the data pointer as unsigned 8-bit values. */
    operator uint8_t*() const { return buffer; }
    /** @brief Return the data pointer as signed 16-bit values. */
    operator int16_t*() const { return reinterpret_cast<int16_t*>(buffer); }
    /** @brief Return the data pointer as unsigned 16-bit values. */
    operator uint16_t*() const { return reinterpret_cast<uint16_t*>(buffer); }
    /** @brief Return the data pointer as signed 32-bit values. */
    operator int32_t*() const { return reinterpret_cast<int32_t*>(buffer); }
    /** @brief Return the data pointer as unsigned 32-bit values. */
    operator uint32_t*() const { return reinterpret_cast<uint32_t*>(buffer); }
    /** @brief Return the data pointer as signed 64-bit values. */
    operator int64_t*() const { return reinterpret_cast<int64_t*>(buffer); }
    /** @brief Return the data pointer as unsigned 64-bit values. */
    operator uint64_t*() const { return reinterpret_cast<uint64_t*>(buffer); }
    /** @brief Return the data pointer as void pointer. */
    operator void*() const { return reinterpret_cast<void*>(buffer); }

    struct Wasm {
        static uint8_t* alloc(int32_t newSize, int32_t prealloc) {
            return ControlBlock::alloc(newSize, prealloc)->buffer;
        }
        static void inc(uint8_t* buffer) {
            ControlBlock::inc(ControlBlock::get(buffer));
        }
        static void dec(uint8_t* buffer) {
            ControlBlock::dec(ControlBlock::get(buffer));
        }
        static uint8_t* resize(uint8_t* buffer, int32_t newSize, int32_t preallocateSize, bool grow, bool copyContent) {
            return ControlBlock::resize(ControlBlock::get(buffer), newSize, preallocateSize, grow, copyContent)->buffer;
        }
    };

};


#endif // CUCUT_BUFFER_HPP
