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

If separation extension is done with bigger tool, check for leftovers inside extension area.
If there are some, remove them using separation tool.


### Converting list of coordinates to G-code

Methoda dla prostych:

1. Bierzemy pierwszy punkt
2. Ustalamy limit nachylenia prostej na dowolny kąt (pozwalamy na dowolną prostą)
3. Bierzemy następny punkt, łączymy prostą z pierwszym i sprawdzamy czy nachylenie prostej jest w wyznaczonych limitach, jeżeli nie, to poprzedni punkt był ostatnim punktem prostej i zaczynamy wyznaczanie od początku
4. Jeżeli ten punkt nie dalej niż MAX od pierwszego, nic nie robimy, idziemy do p. 3
5. Wyznaczamy limity nachylenia prostej, tak żeby ten punkt nie był dalej od prostej niż MAX
6. Wyznaczamy część wspólną z aktualnym zakresem, jeżeli nic nie zostało, to ten jest ostani punkt prostej, idziemy i zaczynamy wyznaczanie od początku
7. idziemy do p. 3

Wskazówka 1: Przy wyznaczaniu limitów, przy pierszym punkcie oddalonym o więcej niż MAX, można wyznaczyć przekształcenie liniowe, pierszy punkt był w (0,0), a drugi w (x,0), gdzie x jest dodatnie. Od tego momentu cała geometria jest przekształcana przez to przekształcenie, więc analiza może być uprostrzona.

Wskazówka 2: (wyklucza wskazówkę 1) Jeżeli przekształcimy każy punkt tak, że początkowy punkt był w (0,0), a aktualny w (x,0), to łatwiej będzie wyznaczyć wektory ograniczające prostą. Następnie wracamy do oryginalnego układu z tymi wektorami. Czyli teraz mamy razem 4 wektory razem z wektorami wyznaczającymi dopuszczalny zakres. Używając iloczynów wektorowych 2D można wyznaczyć które wektory powinny pozostać do wyznaczania nowego zakresu dopuszczalnego (chyba).

Optymalizacja 1: jeżeli w punkcie 3 trafiliśmy na nie pasujący punkt, to można jeszcze przejechać po kilku punktach - może następne będą spełniać warunki

Optymalizacja 2: Po wyznaczeniu całej ścieżki można przejść po węzłach i sprawdzić, czy przesunięcie węzła do tyłu lub do przodu (po oryginalnych punktach) nie zmniejszy błąd średniokwadratowy sąsiadujących odcinków. Jeżeli się da, to porawiamy i zaznaczamy sąsiadujące węzły jako czekające na poprawę. Kończymy jak już nie ma co poprawiać, albo już poprawiliśmy wystarczająco dużo.

Przekształcenie prostych w łuki:

1. Startujemy z jednym węzłem i bierzemy sąsiadujące węzły.
2. Robimy przekształcenie liniowe wszystkich punków między tymi wezłami (włącznie), że węzły są na osi X symetrycznie względem osi Y.
   ```
           ^
           |  (x,y)
           |  o         - punkt przetwarzany
   ---o----+----o---->  - węzły
   (-d,0)  |  (d,0)
           |
           x (0,p)      - środek okręgu
   ```
3. Dla każdego punktu w pomiędzy węzłami wyznaczamy minimalną i maksymalną wartość p. Powinno sprowadzić się do  rozwiązania równań kwadratowych. Odatkowo p nie może być większy niż ileśtam (np. 0 lub -d/2), żeby nie tworzyć zbyt ostrych łuków.
4. Wyznaczamy część wspólną tego zbioru dla kolejnych punktów. Jeżeli część wspólna jest pusta, to nie można tu zrobić łuku. Też trzeba dodać dodatkowe warunki, żeby nie tworzyć dziwnych łuków, np. węzły są zbyt blisko siebie lub maksymalne p to 4 pixele. 
5. Wyznaczamy wartość p:
   - proste podejście: srodej zbioru dopuszczalnego
   - optymalne podejście: minimalizacja błędu średiokwadratowego
6. Wyznacz błąd średniokwadratowy dla takiego łuku i zapamiętaj.
7. Powtórz to samo dla łuku po przeciwnej stronie osi i pozostaw ten lepszy wynik.
8. Przesuwamy się o jeden węzeł i powtarzamy.

W ten sposób otrzymaliśmy listę wezłów, które możemy usunąć i puścić tam łuk między sąsiadującymi.
Do tego lista jest posortowana po błędzie (od najmniejszego).

Teraz redukujemy węzły przez tworzenie łuków:
1. Jeżeli jest wszystkich węzłów (nie tylko na liście) mniej niż 4, to kończymy.
2. Bierzemy pierwszego kantydata z listy i usuwamy z listy
3. Przekształcamy w łuk (p jest już wcześniej wyznaczone)
4. Usuwamy środkowy węzeł i dwa elementy z listy, które używały tego węzła.
5. Wyznaczamy (jak powyżej) koszt utworzenia łuku między wezłami, których aktualnie brakuje w liście i dodajemy je do listy (posortowane)
6. Powtarzamy od p. 1


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

struct BinJSONBoolean {
  uint8_t type; // = 2
  uint32_t value; // false is zero, true is non-zero
};

struct BinJSONNumber {
  uint8_t type; // = 3
  double value;
};

struct BinJSONString {
  uint8_t type; // = 4
  uint32_t length; // String length in bytes (number of characters may be less)
  char text[length] // Only UTF-8 encoded string
};

struct BinJSONArray {
  uint8_t type; // = 5
  uint32_t dataSize; // total size of the following data in bytes
  uint32_t count; // number of array elements
  uint8_t buffer[]; // Buffer containing all elements, packed, without padding
};

struct BinJSONObject {
  uint8_t type; // = 6
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

### Commands

#### Common types

Those types are commonly used in the core library interface.

```ts

interface PixelRectangle {
  // Top left pixel of rectangle (inclusive)
  begin: PixelLocation;
  // Bottom right pixel of rectangle (exclusive)
  end: PixelLocation;
}

interface PixelLocation {
  x: number;
  y: number;
}

```

#### labeler

Find separated objects on the bitmap, label them and provide information about each label.

```ts

interface LabelerCommand {
  command: "labeler";
  // Bitmap to process, zero pixels are background, nonzero are objects
  bitmap: PixelMapRaster;
  // True to use 8-Neighborhood, false or empty for 4-Neighborhood
  is8Neighborhood?: boolean;
}

interface LabelerResponse {
  // Labeled raster, pixels -1 are background, labels start at 0, and holes use -2 and lower.
  labeled: LabelRaster;
  // Information about each label
  info: {
    // Pixel value with that label (is the same as `info` array index)
    index: number;
    // Number of pixels at this object
    pixelCount: number;
    // Location of leftmost pixel of topmost row
    topLeft: PixelLocation;
    // Bounding box of the object
    bbox: PixelRectangle;
    // Background regions enclosed by this object; contained objects are reported separately.
    holes: {
      // Negative pixel value used in the labeled raster, starting at -2
      index: number;
      pixelCount: number;
      // First detected pixel in this hole
      location: PixelLocation;
      bbox: PixelRectangle;
    }[];
  }[];
}

```

#### edt

Create image with euclidean distances to object.
Using "Felzenszwalb and Huttenlocher's Euclidean Distance Transform Algorithm".

This is generic command that can work with different kinds of raster. It takes pixelBytes of raster
to see now long integer on pixel is. It can be 1, 2, 4, or 8. It takes condition telling which pixels are object
and which are not. If condition is true, specified pixel is an object.

It uses `float` type for computing.

```ts

interface EdtCommand {
  command: "edt";
  // Image to process (raster of integers of size 8, 16, 32, or 64 bits)
  bitmap: Raster<T>;
  // Comparition operator, `s` prefix means signed comparition, `u` means unsigned comparition.
  objectCondition: "==" | "!=" | "s<" | "s>" | "u<" | "u>";
  // Value to compare with
  objectValue: number;
  // If raster pixel is 8 bytes long, single number cannot hold 64-bit integer. Use this value for higher 32-bits for 64-bit integer. If not provided, assume zero.
  objectValueHi32?: number;
  // Bounding box of area to process. If provided, process only this area and return raster of the same size as this area.
  bbox?: PixelRectangle;
}

interface LabelerResponse {
  // Raster containing distances from detected objects. The same size as bbox (if provided) or input bitmap.
  distance: Raster<float>;
}

```

#### separation

Create a separation path for specific track.
A separation path is a CNC tool path in 2D plain that separates specific PCB track or plane from the rest of the Cu on the board. The plains can also have inner holes, so this command also allow to separate hole in a separate call.

The command takes euclidean distances from object as raster (e.g. output from `edt` command), distance from object where path should go (`R`) and direction: CW or CCW.
As the result, the command returns a buffer of `float` pairs containing x and y coordinates of points at separation path. The path can be later used to create G-code, but not in this command.

The path from separation command is always closed.

The method is following:
1. Find two pixels next to each other that one has `distance[A] <= R`. Let's call it A. Second called B has `distance[B] >= R`. During this algorithm, A and B will remain those conditions. The method of finding those pixels will be described later.
2. Add the coordinates in the middle of A and B to the result path.
3. Mark pixels (on helper raster) at A and B as visited.
4. Calculate vector V from A to B: `V = B - A`. It should be 1 pixel long.
5. Calculate vector D depending on direction:
   - CW: `D = [V.y, -V.x]`
   - CCW: `D = [-V.y, V.x]`
6. Move points A and/or B depending on case:
   - `distance[A + D] >= R`, then `B = A + D`, A unchanged
   - `distance[B + D] >= R`, then `A = A + D` and `B = B + D`
   - otherwise `A = B + D`, B unchanged
7. If both A and B are at pixels that were marked as visited, finish the algorithm
8. Go back to point 2.

Notice that coordinates of A, B, V, and D are always integers, coordinates in path are `float`.

The method of finding first two points depends on which edge of object we are processing: outer or inner.

Outer edge:
1. The starting point provided by the caller must be inside object (distance == 0).
2. Go up pixel by pixel.
3. If you hit pixel where `distance[B] >= R`, keep this pixel as B and pixel from previous step as A.

Inner edge:
1. The starting point provided by the caller must be inside hole: distance > 0, but less than R.
2. Look at 4 neighborhood of the point and go to point with greatest distance. If none of the points in neighborhood has distance greaten than current pixel, do slower fallback algorithm.
3. If you hit pixel where `distance[B] >= R`, keep this pixel as B and pixel from previous step as A.

Inner edge (slower fallback algorithm)
1. You can use point from previus fast algorithm as starting point.
2. Travel over pixels with distance > 0 like in flood fill with 4 neighborhood. Use helper raster to keep track of visited pixels.
3. If you hit pixel where `distance[T] >= R`, keep this point as T.
4. Go around 8 neighborhood of point T and find point A where `distance[A] < R`.
5. Go around 4 neighborhood of point A and find point B where `distance[A] >= R`.

Warning: if during traveling, we hit edge of provided raster of distances, we should stop processing and return error: BinJSON::Object with single member called "error" with error message. The caller is responsible for passing raster with objects far enough from edges.

```ts

interface SeparationCommand {
  command: "separation";
  // Distance to process, object pixels are 0, distances are > 0. Can be output of "ecd" command.
  distance: Raster<float>;
  // Offset where distance raster is located relative to original image. Output coordinates will be increased by this offset. The startingPoint is in original image coordinates, so before processing, the offset is substracted from it. It is (0, 0) by default.
  offset?: PixelLocation;
  // Radius R of the separation tool (distance from object where path will go)
  radius: number;
  // Direction of path
  direction: "cw" | "ccw";
  // Value to compare with
  edge: "inner" | "outer";
  // Starting point of processing. For "inner" this is some pixel outside object (distance > 0), for "outer" this is some pixel inside object (distance == 0). Those pixels can be obtain from "labeler" command.
  startingPoint: PixelLocation;
}

interface LabelerResponse {
  // Array of float pairs (x and y) of point at the path. The points takes entire buffer, so the count can be calculated from buffer size.
  path: Buffer<float>;
}

```

#### optimizePath

Optimize a closed path by removing points that are within a maximum distance of a replacement line segment. The input path is a buffer of float x/y pairs, such as the output from `separation`, and `maxError` is the finite, non-negative maximum distance in pixels. The command uses the distance to the finite segment (not the infinite line) and retains points in original order. It treats the path as cyclic: the last retained point connects back to the first.

```ts
interface OptimizePathCommand {
  command: "optimizePath";
  path: Buffer<float>;
  maxError: number;
}

interface OptimizePathResponse {
  // Retained points as float x/y pairs.
  path: Buffer<float>;
  // Input point index corresponding to each retained point.
  indices: Buffer<int32>;
}
```

The `indices` buffer contains one signed 32-bit input index per output point. A path with one or two points is returned unchanged. Invalid, non-finite coordinates and malformed buffers are rejected.

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
