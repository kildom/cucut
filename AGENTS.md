# Project

The project name is CuCut. It is a software that converts PCB production files into G-code for CNC milling machines.

Unlike other software, CuCut works on raster images, the input is converted into a bitmap is not already a bitmap. Then, it does a raster processing to generate raster paths of milling. As the final step, it generates G-code from the raster paths.

The core of the software is written in C++. It does not provide a GUI. The GUI will be written later in TypeScript where core will be used as a WebAssembly module. The GUI will be a web application that can be run in a browser. The GUI will be able to run on any platform that supports WebAssembly.

Current goal is to provide stable core library with CLI interface. The workflow involves human and tool cooperation, so it is not possible to do everything in one CLI command. Instead, each stage that does not involve human interaction will be single command. Once executed, user can do further adjustments and execute next command that will do next stage for the workflow.

The CLI will also allow to test core library without need for browser.

Core library works with pixels and it is the only distance measurement unit there. The GUI is responsible for converting it to physical units based on resolution of the bitmaps. The resolution is not changing and it it provided by user at the beginning.

## Workflow

The software accepts three kinds of input:
- tracks - copper tracks that also include copper zones
- cutting - outlines for cutting the PCB including inner cutouts
- drilling - circular holes for drilling

Each of these inputs are processed separately in a different way.

### Tracks

Input bitmap that shows one layer of copper is initially processed with Connected-component labeling (CCL).
Each component gets a integer number (label). For purpose of this document the component will be called track.

Based on that information, user can choose how to separate each track from the rest of the copper by choosing a tool.

User can optionally extend the separation by specifying:
- separation distance - expected width of separation gap
- separation tool - it will be used to extend the separation
- separation pass offset - percentage of separation tool width that will overlap with already cleared area to  avoid small leftovers.

The tool is a set of parameters that define how the milling will be done. The parameters are:
- tool name - user friendly name of the tool
- milling width - the width of the milling tool
- milling depth - the depth of the milling tool
- milling speed - the speed of the milling tool
- milling machine tool index - the index of the milling tool that will be used in the G-code.
- milling direction - CCW by default, but CW is also allowed

#### Track separation

Each track is separated by selected tool by separation path. The method of creating separation path:
- find one of bottom-most pixels of the track
- go down by radius of the tool (counted in pixels)
- the first point of separation path is a point between two pixels: pixel with distance <= tool radius, pixel with distance > radius
- go right (CCW) or left (CW) to the next point of separation path that is also between two pixels fulfilling that above condition. Calculating distance to the track can be calculated on the fly to reduce unnecessary processing as described below.
- go further this direction over points that met above condition until you get back to the starting point.
- you have a path of points that later will be converted to G-code path.

Future optimization will also look at nearby tracks to avoid cutting them too much. Instead, the path should go in the middle of two tracks to minimize track copper loss.

#### Separation extension

The separation extension is done in following way:
- find area extended by provided distance.
- remove following pixels from this area:
  - simulate track separation paths to get pixes that were already removed and does not to be in above area
  - simulate cutting (not just path, but entire cut off areas) and its extension and remove it from above area
  - we need to remove (from this set) pixels that are too close to any of the track on the board.
    - create map of pixels reachable by this tool that will not cut any track. Add margin between track and this tool to avoid even touching the track.
    - remove pixels that are not on this map
  - pixels removed during previous tracks separation extension (simulated).
- create path that removes those pixels (t.b.d. how to do it). Remember to use tool smaller by defined offset.
- simulate this path and skip pixels that were already removed when going to next track.

## Core interface

Core library interface is using structures similar to JSON, but binary instead to reduce parsing on C++ side.
The core idea is similar to CBOR, but the structure is different. It is called binJSON.

Core library has a single request-response interface. It is used both for CLI and GUI.

### binJSON

The binJSON encodes one of the supported types at top.

```c++
// The following pseudo code shows the data layout of each supported binJSON type.
// This is not actual code, it just shows how data is organized in binJSON file, but not in runtime memory after parsing the file.
// All of those structures are packed. All integers are little-endian.

struct BinJSONNull {
  uint8_t type; // = 0
};

struct BinJSONBinary {
  uint8_t type; // = 1
  uint32_t bufferIndex; // Buffer index in array of buffer provided together with this binJSON
};

struct BinJSONNumber {
  uint8_t type; // = 2
  double value;
};

struct BinJSONString {
  uint8_t type; // = 3
  uint32_t length; // String length in bytes (number of characters may be less)
  char text[length] // Only UTF-8 encoded string
};

struct BinJSONArray {
  uint8_t type; // = 4
  uint32_t dataSize; // total size of the following data in bytes
  uint32_t count; // number of array elements
  uint8_t buffer[]; // Buffer containing all elements, packed, without padding
};

struct BinJSONObject {
  uint8_t type; // = 5
  uint32_t dataSize; // total size of the following data in bytes
  uint32_t count; // number of object properties
  struct {
    uint32_t length; // property name length in bytes
    char text[length] // property name
    uint8_t buffer[]; // Buffer containing binJSON value
  } properties[count];
};


```

### WebAssembly interface

The JavaScript/TypeScript communicates with core library WebAssembly module using following API:

#### Exports

* `uint8_t* alloc(int32_t newSize, int32_t prealloc)`

  Allocate new dynamic memory. Additional parameter `prealloc` requests additional space after the allocated block to allow further grow.

  The size of the block is in `uint32_t` value just before allocated blocked - 4 bytes before returned value.
  Application can access it to get block size. It does not include preallocated space. Application cannot modify
  it directly, it should use `resize` exported function.

  Allocated memory is reference counted and the counter is managed by `inc` and `dec` functions. The `alloc` function returns block with reference counter 1. When counter reach 0, block is deallocated.

  Unlike standard C malloc, allocated memory has application-accessible size and it adds reference counting.

* `void inc(uint8_t* buffer)`

  Increment reference counter of the allocated block.

* `void dec(uint8_t* buffer)`

  Decrement reference counter of the allocated block. Deallocate block when it reached 0.

* `uint8_t* resize(uint8_t* buffer, int32_t newSize, int32_t preallocateSize, int32_t grow, int32_t copyContent)`

  Resize allocated buffer. The buffer may or may not be reallocated depending of current allocation size and requested new size. New buffer pointer is returned. When resizing buffer shared between multiple owners (ref counter greaten than 1), reallocation happens in context of single owner, so after resize, other owner may still hold old memory pointer. Both pointers, in this case, are valid and can be used as usual.

  The `preallocateSize` is a hint for allocator to allocate more space for further buffer expansion. It is hint and may not be respected.

  The `grow` flag hints that this buffer may grow and there is no point of shrinking allocated size if requested size is significantly smaller. Unused space will be treated as preallocated size for further grow.

  The `copyContent` flag tells that current content of the buffer is important and must be copied to new memory block if reallocated.

  This function handles correct reference counter adjustments if needed.

* `uint8_t* request(uint8_t* inputBufferPtr)`

  Send a request to core library and get response.

  The `inputBufferPtr` buffer contains array of pointers to buffers. First buffer contains binJSON request.
  Other buffers are needed to decode binJSON data. The ownership of all input buffers and buffer containing array is not passed to the function.

  After execution, the function returns a buffer containing array of pointer to buffers. The ownership of
  all of those is transferred to the caller. First array item is binJSON containing response.

## CLI

The CLI is just a wrapper for core library interface.

Front-end is written in TypeScript. The usage is following:

```
npx tsx src/cucutcli.ts <output.json> <input.json>
```

The front-end:
- Creates array of arguments for backend.
- Put first argument `<output.json>` to backend arguments array, but remove extension. Let's call it `<output-stem>`.
- Takes second parameter `<input.json>` and converts to temporary binJSON file.
- Puts this temporary file to second element of arguments array.
- During conversion, each additional file is added to arguments array starting from third element.
- Executes backend binary with array of arguments.
- Convert backend output binJSON to JSON and write result to first argument `<output.json>`.


The conversion of types between JSON and binJSON in obvious with exception of BinJSONBinary.

When converting to binJSON and string starts with `#FILE#` followed by file name, then:
- put that file to an array of arguments (keep the index where it was added)
- write BinJSONBinary structure with bufferIndex equals to index where file name was added minus 1.

When converting from binJSON and we have BinJSONBinary structure, convert it to a string `#FILE#<output-stem>-<N>.raw`, where `<N>` is bufferIndex from BinJSONBinary.

Back-end is written in C++ and it is linked with core library. It is just a wrapper for core library request-response API. The usage is following:

```
cucutcli-core <output-stem> <input-0> <input-1> ...
```

The back-end:
- Reads all `<input-N>` files to a buffers and creates an array of buffers out of them.
- Pass array of buffers to core library request function.
- Write each element of response array of buffers to an output files. The output file name is created by appending `-<N>.raw` where `<N>` is the index in the output array. Notice that core library always puts response binJSON to first element, so the response binJSON is always in a file `<output-stem>-0.raw`.


## Core library unit tests

Core library unit tests use gtest and gmock. Put test sources in `core/test/src`; the test Makefile builds them together into one binary. Build and run the suite from the repository root with:

```
make -C core/test run
```

Each test source starts with `#include "includes.hpp"`. That header includes the standard-library headers used throughout the project, as well as gtest and gmock; do not include those again in individual test sources. Include all standard-library headers that code under test may use here, because including a missing standard header from inside a test namespace can cause compilation errors. If unit tests produce unexpected compilation errors involving standard-library headers, check that `includes.hpp` includes all required headers.

```c++
#include "includes.hpp"

namespace buffer_test {

#include "buffer.hpp"
#include "buffer.cpp"

TEST(BufferTest, Example)
{
    // Test the included implementation here.
}
}
```

The namespace name is the test source file name without its extension. All code under test belongs inside that namespace: include its header and source files directly, rather than linking the source separately. Include any dependent implementation (`.cpp`) files there as well; omitting one can lead to linker errors. Write the tests in the same namespace so they can refer to the implementation symbols normally. Each test source can use its own namespace, allowing multiple instances of the implementation with different preprocessor configurations in one test binary.

If a test needs to access private or protected members, temporarily redefine the access keywords around the implementation includes, then immediately undefine them:

```c++
#include "includes.hpp"

namespace buffer_test {
#define private public
#define protected public
#include "buffer.hpp"
#include "buffer.cpp"
#undef private
#undef protected

// Tests go here.
}
```

Prefer testing observable behavior through the public interface; use access overrides only when behavior cannot reasonably be tested otherwise.
