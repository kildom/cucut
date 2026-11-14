#ifndef CUCUT_TEST_FATAL_ERROR_TO_EXCEPTION_HPP
#define CUCUT_TEST_FATAL_ERROR_TO_EXCEPTION_HPP

// Include this inside the test namespace before the code under test to replace
// fatalError with an exception that unit tests can catch.

#define fatalError fatalErrorOriginal
#include "errors.hpp"
#undef fatalError

[[maybe_unused]] static auto originalFatalError = &fatalErrorOriginal;

class FatalErrorException : public std::runtime_error
{
public:
    explicit FatalErrorException(const char* message)
        : std::runtime_error(message)
    {
    }
};

[[noreturn]] static void fatalError(const char* message)
{
    throw FatalErrorException(message);
}

#endif // CUCUT_TEST_FATAL_ERROR_TO_EXCEPTION_HPP
