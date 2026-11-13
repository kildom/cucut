---
description: "Use when writing or reviewing C++ code in .cpp and .hpp files. Covers modern C++17 style, correctness, safety, and maintainability."
name: "C++ Guidelines"
applyTo: "**/*.cpp, **/*.hpp"
---

# Code Guidelines

- Use C++17.
- The project has RTTI disabled; don't use it.
- The project has exceptions disabled; don't use them, instead:
  - Call `fatalError` from `errors.hpp` in case of unrecoverable error like memory exhaustion.
  - Use `CUCUT_ASSERT` to check conditions that should never happen, but should be checked in debug build like null parameter when not allowed.
  - Use `CUCUT_ASSERT_NON_NULL` that works similar to `CUCUT_ASSERT`, but check if pointer is not null and it is not close to null (useful to check accessing member of structure pointer pointing to null).
- Prefer RAII, value semantics, and explicit ownership over manual memory management.
- Avoid raw pointers for ownership; use smart pointers (`std::unique_ptr`, `std::shared_ptr`) when dynamic allocation is necessary; use locally implemented smart pointers like `Buffer`.
- Do not use `new` or `delete` directly in application code unless there is a very clear and justified need.
- Prefer forward declarations in headers when practical to reduce include coupling.
- Use `const` consistently and pass parameters by reference when avoiding copies is important.
- Prefer `const auto&` for read-only references to avoid unnecessary copies.
- Keep functions small, focused, and easy to read. Prefer clear names over clever implementations.
- Prefer `enum class` over unscoped enums to avoid name collisions and improve type safety.
- Prefer `constexpr`, `constexpr if`, and `inline` as appropriate for compile-time and performance-sensitive code.
- Avoid unnecessary global state and mutable singletons unless there is a strong architectural reason.
- Favor composition over inheritance when a simple data-oriented design is enough.
- Prefer signed integer `int32_t` for sizes, offsets, indexes like in Java to avoid overflow errors near the zero; add appropriate asserts or runtime checks for negative values instead of replacing signed type by unsigned; document that variable od parameter cannot be negative.
- Validate assumptions explicitly, especially around indexes, ranges, and external inputs using:
  - `CUCUT_ASSERT` if value comes from internal source and the caller/assigner is responsible for providing valid value.
  - normal `if` with error value return if value comes from external input or the caller/assigner may provide invalid value.
- Prefer clear error handling with explicit return codes, but do not hide failure states.
- When checking for errors, prefer explicit conditions and clear messages over silent fallthrough.
- Use `std::move` only when ownership transfer is intentional.
- Write deterministic code and avoid undefined behavior: validate pointers, bounds, and casts.
- Keep comments focused on intent, invariants, and non-obvious decisions; avoid restating obvious code.
- Prefer `auto` when it increases readability and does not hide important type information from the reader.
- For `switch` statements, handle all enum values intentionally and prefer `default` only when you intentionally want a safe fallback.
- Keep formatting consistent, readable, and project-appropriate; avoid deeply nested logic when a small helper function improves clarity.
- Avoid `using namespace ...` in headers or broad scopes; prefer scoped names.
- Prefer `std::array` for fixed-size containers and `std::vector` for dynamic-size containers.
- Keep headers stable and compatible with C++17; do not rely on compiler-specific extensions unless the project explicitly requires them.
- Prefer clear ownership boundaries between interfaces, implementations, and data models.
- Keep tests and validation focused on behavior and edge cases, not just happy-path execution.
- When in doubt, choose correctness, explicitness, and maintainability over cleverness.

## Style Notes

- Prefer `camelCase` for functions and variables.
- Prefer `PascalCase` for types and classes.
- Prefer `UPPER_CASE` for preprocessor definitions and macros.
- Keep class interfaces explicit and minimal.
- Separate public API from implementation details when possible.
- Use `nullptr` instead of `NULL` or `0` for pointers.

## Header Guidance

- Keep `#include` dependencies minimal and ordered.
- Protect headers with include guards consistently; use `CUCUT_<header_name>_HPP` for include guard definition.
- Keep declarations readable and avoid exposing implementation details in public headers.
- Put template and inline definitions where they are used or in clearly named implementation sections.

## Anti-patterns to Avoid

- Manual memory management with raw pointers and custom deletion patterns.
- Large monolithic files with unrelated responsibilities.
- Overuse of macros instead of typed constants or inline functions.
- Unchecked integer conversions, narrowing conversions, and unsafe pointer casts.
- Hidden side effects in constructors, setters, or long-lived utility functions.
- Excessive template metaprogramming when simpler C++17 code would be easier to maintain.

## Documentation Comments

- Use doxygen comment to document functions, variable or other symbols.
- Avoid describing parameter in a one-parameter-functions when it is obvious from the description of the function.
- Avoid describing returned value when no parameters are described and the returned value is obvious from the description of the function.
- Always use `@brief` at the beginning. It may be only text in the comment when it explains everything.
- Always describe details of unusual or non-obvious behavior of the function.
- Tell the limits of the parameters; especially when using signed integer when expecting non-negative value.
- Document also internal and private symbols.
