#ifndef CUCUT_BUFFER_HPP
#define CUCUT_BUFFER_HPP

#include <cstdint>
#include <type_traits>
#include <utility>

#include "rawbuffer.hpp"

/** @brief Reference-counted byte buffer with a typed data pointer.
 *
 * All sizes and capacities are in bytes, regardless of T. Copies and typed
 * casts share the allocation; changing its size detaches a shared allocation.
 * Constness applies to the wrapper, not to the underlying data. A moved-from
 * buffer may be destroyed or assigned to, but cannot be resized or queried.
 */
template<class T = uint8_t>
class Buffer
{
    static_assert(std::is_trivially_copyable_v<T>, "Buffer elements must be trivially copyable");
    static_assert(alignof(T) <= RawBuffer::alignment, "Buffer element alignment exceeds the allocation alignment");

    uint8_t* buffer;

public:
    /** @brief Construct an allocated buffer with logical size zero. */
    Buffer() : buffer(RawBuffer::alloc(0, 0)) {}

    /** @brief Allocate a buffer with a non-negative size and preallocation, in bytes. */
    explicit Buffer(int32_t size, int32_t prealloc = 0)
        : buffer(RawBuffer::alloc(size, prealloc)) {}

    /** @brief Share a non-null live raw buffer, adding one reference. */
    explicit Buffer(void* rawBuffer) : buffer(static_cast<uint8_t*>(rawBuffer))
    {
        CUCUT_ASSERT(rawBuffer != nullptr, "Null raw buffer!");
        RawBuffer::inc(buffer);
    }

    /** @brief Share another non-moved-from buffer's allocation. */
    Buffer(const Buffer& other) : buffer(other.buffer)
    {
        CUCUT_ASSERT(buffer != nullptr, "Cannot copy a moved-from buffer!");
        RawBuffer::inc(buffer);
    }

    /** @brief Transfer an allocation from a non-moved-from buffer. */
    Buffer(Buffer&& other) noexcept : buffer(other.buffer)
    {
        CUCUT_ASSERT(buffer != nullptr, "Cannot move a moved-from buffer!");
        other.buffer = nullptr;
    }

    /** @brief Release this buffer's reference. */
    ~Buffer()
    {
        if (buffer) {
            RawBuffer::dec(buffer);
        }
    }

    /** @brief Share another non-moved-from buffer's allocation. */
    Buffer& operator=(const Buffer& other)
    {
        return *this = static_cast<void*>(other.buffer);
    }

    /** @brief Transfer another non-moved-from buffer's allocation. */
    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other) {
            CUCUT_ASSERT(other.buffer != nullptr, "Cannot move a moved-from buffer!");
            if (buffer) {
                RawBuffer::dec(buffer);
            }
            buffer = other.buffer;
            other.buffer = nullptr;
        }
        return *this;
    }

    /** @brief Share a non-null live raw buffer, adding a reference before releasing the old one. */
    Buffer& operator=(void* rawBuffer)
    {
        CUCUT_ASSERT(rawBuffer != nullptr, "Null raw buffer!");
        auto* next = static_cast<uint8_t*>(rawBuffer);
        RawBuffer::inc(next);
        if (buffer) {
            RawBuffer::dec(buffer);
        }
        buffer = next;
        return *this;
    }

    /** @brief Exchange allocations with another buffer. */
    void swap(Buffer& other) noexcept
    {
        std::swap(buffer, other.buffer);
    }

    /** @brief Return the logical size in bytes. */
    int32_t size() const
    {
        CUCUT_ASSERT(buffer, "Null pointer operation!");
        return RawBuffer::size(buffer);
    }

    /** @brief Return the allocated data capacity in bytes, including unused space. */
    int32_t allocatedSize() const
    {
        CUCUT_ASSERT(buffer, "Null pointer operation!");
        return RawBuffer::allocatedSize(buffer);
    }

    /** @brief Ensure this buffer is the sole owner of its allocation.
     *
     * When shared, this buffer receives a copy and releases its reference to
     * the original allocation.
     */
    void unique()
    {
        CUCUT_ASSERT(buffer != nullptr, "Cannot make a moved-from buffer unique!");
        buffer = RawBuffer::unique(buffer);
    }

    /** @brief Resize the buffer, detaching it from other owners if needed.
     *
     * Sizes are in bytes and must be non-negative. If keepContent is true,
     * bytes within both the old and new size are retained. growOnly retains
     * excess capacity when shrinking a uniquely owned allocation.
     */
    void size(int32_t newSize, bool keepContent = true, int32_t preallocateSize = 0, bool growOnly = false)
    {
        buffer = RawBuffer::realloc(buffer, newSize, preallocateSize, growOnly, keepContent);
    }

    /** @brief Resize while retaining capacity where possible.
     *
     * newSize is a non-negative size in bytes. Allocation requests include
     * an additional RawBuffer::alignment + newSize / 4 bytes of capacity.
     */
    void grow(int32_t newSize, bool keepContent = true)
    {
        CUCUT_ASSERT(newSize >= 0, "Size cannot be negative. It is %d.", newSize);
        buffer = RawBuffer::realloc(buffer, newSize, RawBuffer::alignment + newSize / 4, true, keepContent);
    }

    /** @brief Access the typed object stored at the start of the buffer. */
    T& operator*() const
    {
        CUCUT_ASSERT(buffer != nullptr, "Cannot dereference a moved-from buffer!");
        return *raw<T*>();
    }

    /** @brief Access the typed object stored at the start of the buffer. */
    T* operator->() const
    {
        CUCUT_ASSERT(buffer != nullptr, "Cannot dereference a moved-from buffer!");
        return raw<T*>();
    }

    /** @brief Borrow the data pointer without changing its reference count.
     *
     * The pointer is valid only while the allocation remains owned; resizing
     * this buffer may invalidate it. Tret must be an object or void pointer.
     */
    template<class Tret = T*>
    Tret raw() const
    {
        static_assert(std::is_pointer_v<Tret>, "raw requires a pointer type");
        return static_cast<Tret>(static_cast<void*>(buffer));
    }

    /** @brief Return a data pointer with an additional caller-owned reference.
     *
     * This buffer must not be moved-from.
     */
    template<class Tret = T*>
    Tret shareRaw() const
    {
        CUCUT_ASSERT(buffer != nullptr, "Cannot share a moved-from buffer!");
        RawBuffer::inc(buffer);
        return raw<Tret>();
    }

    /** @brief Share the allocation as another trivially copyable element type. */
    template<class U>
    explicit operator Buffer<U>() const
    {
        return Buffer<U>(static_cast<void*>(buffer));
    }
};

#endif // CUCUT_BUFFER_HPP
