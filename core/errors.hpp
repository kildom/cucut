#ifndef CUCUT_ERRORS_HPP
#define CUCUT_ERRORS_HPP

#if defined(__GNUC__) || defined(__clang__)
#define CUCUT_NORETURN __attribute__((noreturn))
#else
#define CUCUT_NORETURN [[noreturn]]
#endif

static CUCUT_NORETURN void fatalError(const char* message)
{
    (void)message;
    std::abort(); // TODO: Real fatal error handling
}


#define CUCUT_ASSERT(condition, ...) // TODO: Implement assert in debug build
#define CUCUT_ASSERT_NON_NULL(pointer) CUCUT_ASSERT((uintptr_t)(pointer) + (uintptr_t)1024 > (uintptr_t)2048, "NULL pointer access")

#endif // CUCUT_ERRORS_HPP

