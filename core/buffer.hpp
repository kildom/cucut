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
 * A Buffer holds a reference to a 64-byte-aligned data area. Copying a Buffer
 * shares the allocation. Resizing may retain the current allocation or replace
 * it, depending on the requested size, current capacity, and whether the
 * allocation is shared. Callers must not rely on the data pointer remaining
 * unchanged across a resize. A moved-from Buffer holds no allocation and may
 * only be destroyed or assigned to.
 *
 * Constness applies to the Buffer object, not its data: pointer conversions
 * from a const Buffer still provide mutable access. Buffers constructed
 * normally own an allocation even when their logical size is zero.
 */
class Buffer
{
private:

    /** @brief Allocation metadata stored immediately before a buffer's data.
     *
     * The data is aligned to 64 bytes. `allocatedPointer` is the pointer
     * returned by the allocator and may differ from the address of this
     * control block. The block is moved with `memmove` when reallocation
     * changes its alignment; its members must therefore remain trivially
     * copyable.
     */
    struct ControlBlock
    {
        /** @brief Required alignment of the buffer data in bytes. */
        static constexpr int32_t alignment = 64;

        /** @brief Original pointer passed to the allocator. */
        void* allocatedPointer;
        /** @brief Available capacity in bytes starting at `buffer`. */
        int32_t allocatedSize;
        /** @brief Number of Buffer instances sharing this allocation. */
        RefCounter refs;
        /** @brief Reserved; currently unused. */
        int32_t _reserved;
        /** @brief Logical size of the buffer in bytes. */
        int32_t size;
        /** @brief Buffer data, aligned to `alignment`. */
        uint8_t buffer[];

        /** @brief Allocate a buffer and initialize its control block.
         *
         * The logical size is `newSize`; `preallocateSize` adds capacity after
         * that size. The returned allocation has a reference count of one.
         * Both sizes must be non-negative. Allocation failure is reported
         * through `fatalError`.
         *
         * @param newSize Logical buffer size in bytes.
         * @param preallocateSize Additional capacity in bytes.
         * @returns The control block for the new allocation.
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

        /** @brief Reallocate an existing allocation.
         *
         * The allocation is resized to hold `newSize` bytes after the control
         * block. The control block and reference count are preserved, although
         * the returned control-block pointer may differ from @p cbOld.
         * If @p copyContent is true, the first
         * `min(old size, newSize)` data bytes are retained when the control
         * block must move to a differently aligned address. The caller must
         * ensure that this allocation is uniquely owned. @p newSize must be
         * non-negative. Allocation failure is reported through `fatalError`.
         *
         * @param cbOld Control block of the allocation to reallocate.
         * @param newSize New allocation size in bytes, excluding the control block.
         * @param copyContent Whether to preserve existing data when needed.
         * @returns The control block at the allocation's new location.
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

        /** @brief Retrieve the control block associated with a data pointer.
         *
         * @param buffer Pointer to the data area of a live Buffer allocation.
         * @returns The associated control block.
         */
        static ControlBlock* get(void* buffer)
        {
            CUCUT_ASSERT(buffer != nullptr, "NULL pointer access");
            auto cb = reinterpret_cast<ControlBlock*>(buffer - offsetof(ControlBlock, buffer));
            CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");
            return cb;
        }

        /** @brief Deallocate an allocation and its control block.
         *
         * The caller must have already reduced its reference count to zero.
         *
         * @param cb Control block of the allocation to deallocate.
         */
        static void free(ControlBlock* cb)
        {
            CUCUT_ASSERT(cb != nullptr, "NULL pointer access");
            CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");

            cb->~ControlBlock();
            std::free(cb->allocatedPointer);
        }

        /** @brief Set the logical size of an allocation.
         *
         * If the size is unchanged, the existing allocation is returned
         * without changing its reference count. A shared allocation is
         * detached for a size change; the new allocation has one reference
         * and the old allocation loses this reference. A uniquely owned
         * allocation is reused when its capacity and the @p grow policy allow
         * it, otherwise it is reallocated. Reallocation requests
         * `newSize + preallocateSize` bytes. When @p copyContent is true, the
         * first `min(old size, newSize)` bytes are retained in a new
         * allocation.
         *
         * @param cb Control block of the allocation to resize. Do not use this
         * pointer after the call; it may have moved or been deallocated.
         * @param newSize Requested logical size in bytes; must be non-negative.
         * @param preallocateSize Additional capacity requested on allocation;
         * must be non-negative.
         * @param grow If true, reuse any allocation whose capacity can hold
         * the requested size. If false, reuse it only when its capacity is
         * within the implementation's shrink threshold.
         * @param copyContent Whether to retain existing data when allocating.
         * @returns The control block for the resized allocation.
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

        /** @brief Add one reference to an allocation.
         *
         * @param cb Control block of a live allocation.
         */
        static void inc(ControlBlock* cb)
        {
            CUCUT_ASSERT(cb != nullptr, "NULL pointer access");
            CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");
            cb->refs.inc();
        }

        /** @brief Release one reference and deallocate when it was the last.
         *
         * @param cb Control block of a live allocation.
         */
        static void dec(ControlBlock* cb)
        {
            CUCUT_ASSERT(cb != nullptr, "NULL pointer access");
            CUCUT_ASSERT(cb->refs.valid(), "Operation on reallocated or corrupted memory");
            if (!cb->refs.dec()) {
                free(cb);
            }
        }

    };


    /** @brief Data pointer, or null in a moved-from Buffer. */
    uint8_t* buffer;

public:
    /** @brief Construct an allocated buffer with logical size zero. */
    Buffer() : buffer(ControlBlock::alloc(0, 0)->buffer) {}

    /** @brief Construct a buffer with the requested logical size.
     *
     * @param size Logical size in bytes; must be non-negative.
     * @param prealloc Additional capacity in bytes; must be non-negative.
     */
    explicit Buffer(int32_t size, int32_t prealloc = 0) : buffer(ControlBlock::alloc(size, prealloc)->buffer) {}

    /** @brief Share the allocation held by another Buffer. */
    Buffer(const Buffer& other) : buffer(other.buffer)
    {
        ControlBlock::inc(ControlBlock::get(other.buffer));
    }

    /** @brief Transfer the allocation, leaving the source moved-from. */
    Buffer(Buffer&& other) noexcept : buffer(other.buffer)
    {
        other.buffer = nullptr;
    }

    /** @brief Construct a reference from an existing Buffer data pointer.
     *
     * The pointer must be the data pointer of a live Buffer allocation; this
     * constructor increments that allocation's reference count.
     *
     * @param ptr Data pointer of a live Buffer allocation.
     */
    Buffer(void* ptr): buffer((uint8_t*)ptr)
    {
        ControlBlock::inc(ControlBlock::get(buffer));
    }

    /** @brief Release this Buffer's reference to its allocation. */
    ~Buffer()
    {
        if (buffer) {
            ControlBlock::dec(ControlBlock::get(buffer));
        }
    }

    /** @brief Share the allocation held by another Buffer.
     *
     * @param other Buffer whose allocation is to be shared.
     * @returns A reference to this Buffer.
     */
    Buffer& operator=(const Buffer& other)
    {
        ControlBlock::inc(ControlBlock::get(other.buffer));
        if (buffer) {
            ControlBlock::dec(ControlBlock::get(buffer));
        }
        buffer = other.buffer;
        return *this;
    }

    /** @brief Transfer the allocation held by another Buffer.
     *
     * If @p other is this object, no change is made. Otherwise this Buffer's
     * current reference is released and @p other is left moved-from.
     *
     * @param other Buffer whose allocation is to be transferred.
     * @returns A reference to this Buffer.
     */
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

    /** @brief Exchange data pointers with another Buffer.
     *
     * @param other Buffer to exchange allocations with.
     */
    void swap(Buffer& other) noexcept
    {
        std::swap(buffer, other.buffer);
    }

    /** @brief Return the logical size in bytes.
     *
     * This operation requires a Buffer that has not been moved from.
     */
    uint32_t size() const
    {
        CUCUT_ASSERT(buffer, "Null pointer operation!");
        return ControlBlock::get(buffer)->size;
    }

    /** @brief Set the logical size of the buffer.
     *
     * A size change detaches a shared allocation, leaving other Buffer
     * instances unchanged. A unique allocation is reused or reallocated
     * according to its capacity and @p growOnly. If @p keepContent is true,
     * the first `min(old size, newSize)` bytes are retained when a new
     * allocation is required.
     *
     * @param newSize Requested logical size in bytes; must be non-negative.
     * @param keepContent Whether to preserve existing content when allocating.
     * @param preallocateSize Additional capacity requested when allocating;
     * must be non-negative.
     * @param growOnly If true, retain the current allocation whenever it has
     * enough capacity, even if the logical size is reduced. If false, a
     * sufficiently large allocation may be reallocated when shrinking.
     */
    void size(int32_t newSize, bool keepContent = true, int32_t preallocateSize = 0, bool growOnly = false)
    {
        auto cb = ControlBlock::get(buffer);
        auto cbNew = ControlBlock::resize(cb, newSize, preallocateSize, growOnly, keepContent);
        buffer = cbNew->buffer;
    }

    /** @brief Set the logical size, preferring to retain allocated capacity.
     *
     * Despite the name, this also reduces the logical size when @p newSize is
     * smaller. It uses `growOnly = true` when resizing, so a unique allocation
     * is retained whenever it can hold the requested size. If allocation is
     * required, it requests an additional `64 + newSize / 4` bytes of
     * capacity. A shared allocation is detached when its logical size changes.
     *
     * @param newSize Requested logical size in bytes; must be non-negative.
     * @param keepContent Whether to preserve existing content when allocating.
     */
    void grow(int32_t newSize, bool keepContent = true)
    {
        auto cb = ControlBlock::get(buffer);
        auto cbNew = ControlBlock::resize(cb, newSize, 64 + newSize / 4, true, keepContent);
        buffer = cbNew->buffer;
    }

    /** @brief Convert to a pointer to signed 8-bit values.
     */
    operator int8_t*() const { return reinterpret_cast<int8_t*>(buffer); }
    /** @brief Convert to a pointer to unsigned 8-bit values.
     */
    operator uint8_t*() const { return buffer; }
    /** @brief Convert to a pointer to signed 16-bit values.
     */
    operator int16_t*() const { return reinterpret_cast<int16_t*>(buffer); }
    /** @brief Convert to a pointer to unsigned 16-bit values.
     */
    operator uint16_t*() const { return reinterpret_cast<uint16_t*>(buffer); }
    /** @brief Convert to a pointer to signed 32-bit values.
     */
    operator int32_t*() const { return reinterpret_cast<int32_t*>(buffer); }
    /** @brief Convert to a pointer to unsigned 32-bit values.
     */
    operator uint32_t*() const { return reinterpret_cast<uint32_t*>(buffer); }
    /** @brief Convert to a pointer to signed 64-bit values.
     */
    operator int64_t*() const { return reinterpret_cast<int64_t*>(buffer); }
    /** @brief Convert to a pointer to unsigned 64-bit values.
     */
    operator uint64_t*() const { return reinterpret_cast<uint64_t*>(buffer); }
    /** @brief Convert to a mutable void pointer.
     */
    operator void*() const { return reinterpret_cast<void*>(buffer); }

    /** @brief WebAssembly wrapper functions.
     *
     * See the corresponding called function for details.
     */
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
