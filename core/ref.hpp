
#ifndef CUCUT_REF_HPP
#define CUCUT_REF_HPP

#ifndef CUCUT_ATOMIC_REF_COUNTER
#define CUCUT_ATOMIC_REF_COUNTER 1
#endif

#include <cstdint>

#define MAX_REFERENCES (128 * 1024 * 1024) // 128 M references

/**
 * @brief Reference counting helper class.
 *
 * Implemented with a plain uint32_t counter.
 * Atomic operations are optionally enabled using
 * GCC/Clang atomic builtins.
 * 
 * Trivially copyable object and you can copy it using C-style memory
 * copying functions like memcpy and memmove.
 */
class RefCounter
{
    uint32_t counter;

public:
    RefCounter() : counter(1) {}

    /// @brief Increment counter.
    void inc() {
#if CUCUT_ATOMIC_REF_COUNTER
        __atomic_add_fetch(&counter, 1, __ATOMIC_SEQ_CST);
#else
        ++counter;
#endif
    }

    /**
     * @brief Decrement counter.
     * @returns true if counter is still above zero, false otherwise.
     */
    bool dec() {
#if CUCUT_ATOMIC_REF_COUNTER
        return __atomic_sub_fetch(&counter, 1, __ATOMIC_SEQ_CST) != 0;
#else
        return --counter != 0;
#endif
    }

    /// @brief Get current counter value.
    uint32_t get() const {
#if CUCUT_ATOMIC_REF_COUNTER
        return __atomic_load_n(&counter, __ATOMIC_SEQ_CST);
#else
        return counter;
#endif
    }

    /**
     * @brief Check if reference counter is valid
     * (for asserts and debugging).
     */
    bool valid() const {
        if ((uintptr_t)this + (uintptr_t)1024 < 2048)
            return false;

        uint32_t value = get() - 1;

        if (value > MAX_REFERENCES - 1)
            return false;

        return true;
    }
};

static_assert(sizeof(RefCounter) == sizeof(uint32_t),
              "RefCounter must have the same size as uint32_t");

static_assert(alignof(RefCounter) == alignof(uint32_t),
              "RefCounter must have the same alignment as uint32_t");

#endif // CUCUT_REF_HPP
