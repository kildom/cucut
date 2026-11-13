
#ifndef CUCUT_REF_HPP
#define CUCUT_REF_HPP

#ifndef CUCUT_ATOMIC_REF_COUNTER
#define CUCUT_ATOMIC_REF_COUNTER 1
#endif

#include <cstdint>

#if CUCUT_ATOMIC_REF_COUNTER
#include <atomic>
#endif

#define MAX_REFERENCES (128 * 1024 * 1024) // 128 M references


/** @brief Reference counting helper class.
 * 
 * It is implemented with simple integer or atomic depending on CUCUT_ATOMIC_REF_COUNTER preprocessor
 * definition.
 * 
 */
class RefCounter
{

#if CUCUT_ATOMIC_REF_COUNTER
    std::atomic_uint32_t counter;
#else
    uint32_t counter;
#endif

public:
    RefCounter() : counter(1) {}

    /// @brief Increment counter
    void inc() {
        ++counter;
    }

    /** @brief Decrement counter
     * @returns `true` if counter is still above zero, `false` otherwise.
     */
    bool dec() {
        auto newValue = --counter;
        return newValue != 0;
    }

    uint32_t get() const {
        return counter;
    }

    /** @brief Check if reference counter is valid (for asserts and debugging). */
    bool valid() const {
        if ((uintptr_t)this + (uintptr_t)1024 < 2048) return false;
        uint32_t value = (uint32_t)counter - 1;
        if (value > MAX_REFERENCES - 1) return false;
        return true;
    }

};

static_assert(sizeof(RefCounter) == sizeof(uint32_t),
              "RefCounter must have the same size as uint32_t");

static_assert(alignof(RefCounter) == alignof(uint32_t),
              "RefCounter must have the same alignment as uint32_t");

#endif // CUCUT_REF_HPP
