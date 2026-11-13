#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

namespace test_buffer {
#include "../../buffer.hpp"

[[noreturn]] void fatalError(const char*)
{
    std::abort();
}
}

using test_buffer::Buffer;

// Tests default construction. It should allocate a non-null empty buffer
// aligned to 64 bytes; size and pointer alignment are checked directly.
TEST(BufferTest, DefaultConstructionAllocatesAlignedEmptyBuffer)
{
    Buffer buffer;
    auto* data = static_cast<uint8_t*>(buffer);

    ASSERT_NE(data, nullptr);
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(data) % uintptr_t{64}, uintptr_t{0});
}

// Tests zero-size construction and later resizing. Zero-size operations should
// remain valid, and growing to four bytes should make writable storage usable.
TEST(BufferTest, ZeroSizedConstructionAndResizeRemainUsable)
{
    Buffer buffer(0);
    auto* originalData = static_cast<uint8_t*>(buffer);

    buffer.size(0);
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(static_cast<uint8_t*>(buffer), originalData);

    buffer.size(4);
    EXPECT_EQ(buffer.size(), 4);
    static_cast<uint8_t*>(buffer)[0] = 0xA5;
    EXPECT_EQ(static_cast<uint8_t*>(buffer)[0], 0xA5);

    buffer.size(0);
    EXPECT_EQ(buffer.size(), 0);
}

// Tests the maximum allocation limit. Requests exceeding it, including an
// overflowing size-plus-preallocation sum, should terminate through fatalError.
TEST(BufferTest, AllocationRequestsAboveMaximumCapacityDie)
{
    constexpr int32_t maximumCapacity = 1024 * 1024 * 1024;

    EXPECT_DEATH({ Buffer tooLarge(maximumCapacity + 1); }, "");
    EXPECT_DEATH({ Buffer tooMuchPreallocation(0, maximumCapacity + 1); }, "");
    EXPECT_DEATH({ Buffer overflowingCapacity(1, maximumCapacity); }, "");
}

// Tests growth within reserved capacity. The logical size should increase
// without changing the data pointer or the bytes already stored.
TEST(BufferTest, PreallocationAllowsGrowthWithoutReplacingAllocation)
{
    Buffer buffer(4, 256);
    auto* originalData = static_cast<uint8_t*>(buffer);
    originalData[0] = 0xA5;
    originalData[1] = 0x5A;

    buffer.size(200);

    EXPECT_EQ(buffer.size(), 200);
    EXPECT_EQ(static_cast<uint8_t*>(buffer), originalData);
    EXPECT_THAT(std::vector<uint8_t>(originalData, originalData + 2),
                ::testing::ElementsAre(0xA5, 0x5A));
}

// Tests grow() allocating space beyond the original buffer. It should update
// the logical size and preserve all previous bytes.
TEST(BufferTest, GrowPreservesExistingContent)
{
    Buffer buffer(3);
    auto* data = static_cast<uint8_t*>(buffer);
    data[0] = 0x12;
    data[1] = 0x34;
    data[2] = 0x56;

    buffer.grow(12);

    EXPECT_EQ(buffer.size(), 12);
    auto* grownData = static_cast<uint8_t*>(buffer);
    EXPECT_THAT(std::vector<uint8_t>(grownData, grownData + 3),
                ::testing::ElementsAre(0x12, 0x34, 0x56));
}

// Tests grow() after shrinking. grow() should retain capacity so regrowing to
// the former size reuses the allocation and leaves both end bytes intact.
TEST(BufferTest, GrowRetainsCapacityWhenShrinkingAndGrowingAgain)
{
    Buffer buffer;
    buffer.grow(100);
    auto* originalData = static_cast<uint8_t*>(buffer);
    originalData[0] = 0x12;
    originalData[99] = 0x56;

    buffer.grow(20);
    EXPECT_EQ(buffer.size(), 20);
    EXPECT_EQ(static_cast<uint8_t*>(buffer), originalData);

    buffer.grow(100);
    EXPECT_EQ(buffer.size(), 100);
    EXPECT_EQ(static_cast<uint8_t*>(buffer), originalData);
    EXPECT_EQ(originalData[0], 0x12);
    EXPECT_EQ(originalData[99], 0x56);
}

// Tests size() truncation and regrowth inside reserved capacity. The pointer
// should remain stable and all bytes retained by the truncation should survive.
TEST(BufferTest, SizeCanTruncateAndRegrowWithinPreallocatedCapacity)
{
    Buffer buffer(16, 64);
    auto* originalData = static_cast<uint8_t*>(buffer);
    for (int32_t i = 0; i < buffer.size(); ++i) {
        originalData[i] = static_cast<uint8_t>(i + 1);
    }

    buffer.size(8, true, 0, true);
    EXPECT_EQ(buffer.size(), 8);
    EXPECT_EQ(static_cast<uint8_t*>(buffer), originalData);

    buffer.size(32, true, 0, true);
    EXPECT_EQ(buffer.size(), 32);
    EXPECT_EQ(static_cast<uint8_t*>(buffer), originalData);
    EXPECT_THAT(std::vector<uint8_t>(originalData, originalData + 8),
                ::testing::ElementsAre(1, 2, 3, 4, 5, 6, 7, 8));
}

// Tests shrinking a large buffer. The logical size should decrease while bytes
// within the new size remain unchanged.
TEST(BufferTest, ShrinkingPreservesContentWithinNewLogicalSize)
{
    Buffer buffer(1024, 1024);
    auto* data = static_cast<uint8_t*>(buffer);
    data[0] = 0xA5;
    data[1] = 0x5A;

    buffer.size(1);

    EXPECT_EQ(buffer.size(), 1);
    EXPECT_EQ(static_cast<uint8_t*>(buffer)[0], 0xA5);
}

// Tests a shrink whose explicit preallocation keeps it within the reuse
// threshold. The allocation pointer and existing byte should remain unchanged.
TEST(BufferTest, ExplicitPreallocationCanAvoidShrinkingReallocation)
{
    Buffer buffer(64, 512);
    auto* originalData = static_cast<uint8_t*>(buffer);
    originalData[0] = 0xA5;

    buffer.size(1, true, 512);

    EXPECT_EQ(buffer.size(), 1);
    EXPECT_EQ(static_cast<uint8_t*>(buffer), originalData);
    EXPECT_EQ(originalData[0], 0xA5);
}

// Tests resizing a shared buffer with content copying disabled. The resized
// instance should detach, while the other copy keeps its original bytes/size.
TEST(BufferTest, ResizingWithoutKeepingContentDoesNotChangeOtherCopies)
{
    Buffer original(4);
    auto* originalData = static_cast<uint8_t*>(original);
    originalData[0] = 0x12;
    originalData[1] = 0x34;
    originalData[2] = 0x56;
    originalData[3] = 0x78;
    Buffer copy(original);

    original.size(8, false);

    EXPECT_EQ(original.size(), 8);
    EXPECT_NE(static_cast<uint8_t*>(original), originalData);
    EXPECT_EQ(copy.size(), 4);
    EXPECT_EQ(static_cast<uint8_t*>(copy), originalData);
    EXPECT_THAT(std::vector<uint8_t>(originalData, originalData + 4),
                ::testing::ElementsAre(0x12, 0x34, 0x56, 0x78));
}

// Tests copy construction and same-size resize. Copies should share storage,
// and requesting the current size should leave that shared allocation intact.
TEST(BufferTest, CopySharesAllocationUntilResizeDetaches)
{
    Buffer original(3);
    auto* originalData = static_cast<uint8_t*>(original);
    originalData[0] = 0x12;
    originalData[1] = 0x34;
    originalData[2] = 0x56;

    Buffer copy(original);
    EXPECT_EQ(static_cast<uint8_t*>(copy), originalData);

    original.size(3, false, 128);
    EXPECT_EQ(static_cast<uint8_t*>(original), originalData);
    EXPECT_EQ(static_cast<uint8_t*>(copy), originalData);

    original.size(8);

    auto* resizedData = static_cast<uint8_t*>(original);
    EXPECT_NE(resizedData, originalData);
    EXPECT_EQ(original.size(), 8);
    EXPECT_EQ(copy.size(), 3);
    EXPECT_THAT(std::vector<uint8_t>(resizedData, resizedData + 3),
                ::testing::ElementsAre(0x12, 0x34, 0x56));
    EXPECT_THAT(std::vector<uint8_t>(static_cast<uint8_t*>(copy),
                                     static_cast<uint8_t*>(copy) + 3),
                ::testing::ElementsAre(0x12, 0x34, 0x56));
}

// Tests detachment when one of several copies changes size. Only the resized
// copy should receive independent storage; the other copies should retain data.
TEST(BufferTest, MultipleCopiesDetachIndependently)
{
    Buffer original(2);
    auto* data = static_cast<uint8_t*>(original);
    data[0] = 0x12;
    data[1] = 0x34;
    Buffer firstCopy(original);
    Buffer secondCopy(firstCopy);

    firstCopy.size(5);
    static_cast<uint8_t*>(firstCopy)[0] = 0xAB;

    EXPECT_EQ(original.size(), 2);
    EXPECT_EQ(secondCopy.size(), 2);
    EXPECT_EQ(static_cast<uint8_t*>(original), data);
    EXPECT_EQ(static_cast<uint8_t*>(secondCopy), data);
    EXPECT_THAT(std::vector<uint8_t>(data, data + 2),
                ::testing::ElementsAre(0x12, 0x34));
    EXPECT_EQ(static_cast<uint8_t*>(firstCopy)[0], 0xAB);
}

// Tests constructing a Buffer from another buffer's data pointer. It should
// share the allocation, then detach on resize without changing the original.
TEST(BufferTest, ConstructingFromDataPointerSharesAllocation)
{
    Buffer original(2);
    auto* data = static_cast<uint8_t*>(original);
    data[0] = 0x12;
    data[1] = 0x34;

    Buffer reference(static_cast<void*>(data));

    EXPECT_EQ(static_cast<uint8_t*>(reference), data);
    EXPECT_EQ(reference.size(), 2);
    static_cast<uint8_t*>(reference)[1] = 0xAB;
    EXPECT_EQ(data[1], 0xAB);

    reference.size(4);
    EXPECT_EQ(original.size(), 2);
    EXPECT_NE(static_cast<uint8_t*>(reference), data);
    EXPECT_EQ(static_cast<uint8_t*>(original), data);
    EXPECT_EQ(data[1], 0xAB);
}

// Tests copy assignment over an existing allocation. The assigned buffer
// should share writes initially, then detach on resize and preserve the source.
TEST(BufferTest, CopyAssignmentSharesAllocation)
{
    Buffer original(2);
    auto* originalData = static_cast<uint8_t*>(original);
    originalData[0] = 0x12;

    Buffer assigned;
    assigned = original;

    EXPECT_EQ(static_cast<uint8_t*>(assigned), originalData);
    EXPECT_EQ(assigned.size(), original.size());
    static_cast<uint8_t*>(assigned)[0] = 0xAB;
    EXPECT_EQ(originalData[0], 0xAB);

    assigned.size(4);
    EXPECT_NE(static_cast<uint8_t*>(assigned), originalData);
    EXPECT_EQ(original.size(), 2);
    EXPECT_EQ(originalData[0], 0xAB);
}

// Tests self-copy assignment. It should preserve the allocation, logical size,
// and stored bytes; pointer and byte comparisons verify that behavior.
TEST(BufferTest, SelfCopyAssignmentPreservesBuffer)
{
    Buffer buffer(3);
    auto* data = static_cast<uint8_t*>(buffer);
    data[0] = 0x12;
    data[1] = 0x34;
    data[2] = 0x56;

    buffer = buffer;

    EXPECT_EQ(buffer.size(), 3);
    EXPECT_EQ(static_cast<uint8_t*>(buffer), data);
    EXPECT_THAT(std::vector<uint8_t>(data, data + 3),
                ::testing::ElementsAre(0x12, 0x34, 0x56));
}

// Tests reusing moved-from objects through copy and move assignment. Assignment
// should restore a valid allocation without affecting the remaining owner.
TEST(BufferTest, MovedFromBufferCanBeAssignedAgain)
{
    Buffer source(2);
    auto* sourceData = static_cast<uint8_t*>(source);
    Buffer movedFrom(std::move(source));

    source = movedFrom;
    EXPECT_EQ(static_cast<uint8_t*>(source), sourceData);
    EXPECT_EQ(source.size(), 2);

    Buffer replacement(1);
    auto* replacementData = static_cast<uint8_t*>(replacement);
    movedFrom = std::move(replacement);

    EXPECT_EQ(static_cast<uint8_t*>(movedFrom), replacementData);
    EXPECT_EQ(movedFrom.size(), 1);
    EXPECT_EQ(static_cast<uint8_t*>(replacement), nullptr);
    EXPECT_EQ(static_cast<uint8_t*>(source), sourceData);
    EXPECT_EQ(source.size(), 2);
}

// Tests move construction followed by move assignment. Each operation should
// transfer the original allocation to its destination.
TEST(BufferTest, MoveConstructionAndAssignmentTransferAllocation)
{
    Buffer source(2);
    auto* sourceData = static_cast<uint8_t*>(source);

    Buffer moved(std::move(source));
    EXPECT_EQ(static_cast<uint8_t*>(moved), sourceData);
    EXPECT_EQ(static_cast<uint8_t*>(source), nullptr);

    Buffer destination(1);
    destination = std::move(moved);

    EXPECT_EQ(static_cast<uint8_t*>(destination), sourceData);
    EXPECT_EQ(destination.size(), 2);
}

// Tests move-assigning over a buffer that has another owner. The destination
// should take the source allocation while the old allocation's copy stays live.
TEST(BufferTest, MoveAssignmentReleasesOnlyItsReferenceToSharedOldBuffer)
{
    Buffer oldBuffer(1);
    auto* oldData = static_cast<uint8_t*>(oldBuffer);
    oldData[0] = 0xA5;
    Buffer oldBufferCopy(oldBuffer);

    Buffer source(2);
    auto* sourceData = static_cast<uint8_t*>(source);
    sourceData[0] = 0x12;
    sourceData[1] = 0x34;

    oldBuffer = std::move(source);

    EXPECT_EQ(static_cast<uint8_t*>(oldBuffer), sourceData);
    EXPECT_EQ(static_cast<uint8_t*>(source), nullptr);
    EXPECT_EQ(oldBuffer.size(), 2);
    EXPECT_EQ(oldBufferCopy.size(), 1);
    EXPECT_EQ(static_cast<uint8_t*>(oldBufferCopy), oldData);
    EXPECT_EQ(oldData[0], 0xA5);
}

// Tests self-move assignment and self-swap. Both should be no-ops, verified by
// checking that the original allocation, size, and data remain unchanged.
TEST(BufferTest, SelfMoveAssignmentAndSelfSwapAreNoOps)
{
    Buffer buffer(2);
    auto* data = static_cast<uint8_t*>(buffer);
    data[0] = 0x12;

    Buffer* self = &buffer;
    buffer = std::move(*self);
    buffer.swap(buffer);

    EXPECT_EQ(buffer.size(), 2);
    EXPECT_EQ(static_cast<uint8_t*>(buffer), data);
    EXPECT_EQ(data[0], 0x12);
}

// Tests swapping two buffers. Each object should receive the other's pointer
// and size.
TEST(BufferTest, SwapExchangesAllocations)
{
    Buffer first(2);
    Buffer second(5);
    auto* firstData = static_cast<uint8_t*>(first);
    auto* secondData = static_cast<uint8_t*>(second);

    first.swap(second);

    EXPECT_EQ(static_cast<uint8_t*>(first), secondData);
    EXPECT_EQ(static_cast<uint8_t*>(second), firstData);
    EXPECT_EQ(first.size(), 5);
    EXPECT_EQ(second.size(), 2);
}

// Tests the documented const conversion behavior. A pointer obtained from a
// const Buffer remains mutable and writes through to its underlying storage.
TEST(BufferTest, ConstBufferStillProvidesMutableDataPointer)
{
    Buffer buffer(1);
    const Buffer& constBuffer = buffer;
    auto* data = static_cast<uint8_t*>(constBuffer);

    data[0] = 0xA5;

    EXPECT_EQ(static_cast<uint8_t*>(buffer)[0], 0xA5);
}

// Tests all supported typed pointer conversions. They should refer to the same
// storage, whose 64-byte alignment satisfies each converted pointer type.
TEST(BufferTest, TypedPointerConversionsReferToSameAlignedStorage)
{
    Buffer buffer(16);
    auto* data = static_cast<uint8_t*>(buffer);

    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(static_cast<int8_t*>(buffer)));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(static_cast<int16_t*>(buffer)));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(static_cast<uint16_t*>(buffer)));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(static_cast<int32_t*>(buffer)));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(static_cast<uint32_t*>(buffer)));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(static_cast<int64_t*>(buffer)));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(static_cast<uint64_t*>(buffer)));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(static_cast<void*>(buffer)));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(data) % uintptr_t{64}, uintptr_t{0});
}
