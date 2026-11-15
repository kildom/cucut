#ifndef CUCUT_WASM_HPP
#define CUCUT_WASM_HPP

#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)


// TODO: Finalize wasm export 

/** @brief Mark function as export for WebAssembly module.
 * 
 */
#define WASM_EXPORT(name) \
    extern "C"

#endif

#endif // CUCUT_WASM_HPP
