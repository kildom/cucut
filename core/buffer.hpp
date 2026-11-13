
#include <atomic>
#include <cstddef>

#include "ref.hpp"

/** @brief Holds buffer content.
 *
 * The Buffer class holds pointer to `buffer` field only, so if you want to access
 * other fields, you have to use offsetof and pointer substaction to get access to
 * entire BufferArray structure.
 *
 * The BufferArray structure is aligned in a way that ensures the buffer field is
 * aligned to 512 bits (64 bytes). Standard allocator may not support that, so
 * more memory may be allocated and actual pointer use for allocate and deallocate
 * is hold by `allocatedPointer`.
 *
 * This structure is managed with C malloc, realloc, free functions.
 */
struct BufferArray
{
    /** Holds actual pointer where allocated memory starts (for realloc and free functions). */
    void* allocatedPointer;
    /** Buffer reference counter. */
    RefCounter refs;
    /** Buffer size - only in `buffer` field, not entire allocated memory. */
    uint32_t size;
    /** Buffer data */
    uint8_t buffer[];
};


/** @brief Memory buffer.
 * 
 * This class is a reference to the buffer in memory. It is reference counted.
 * 
 * This is lightweight class to hold reference - it just contains pointer to the buffer
 * and handles reference counting. It also allows memory resizing.
 * 
 * It always contains associated memory - it cannot be null.
 */
class Buffer
{
private:
    uint8_t* buffer;

    void init(uint32_t size) {
        // TODO: allocate with preceding BufferArray fields and alignment to 512 bits (64 bytes)
    }
public:

    Buffer() {
        init(0);
    }

    uint32_t size() const
    {
        const auto* array = reinterpret_cast<const BufferArray*>(buffer - offsetof(BufferArray, buffer));
        return array->size;
    }

};

