#include "includes.hpp"

namespace buffer_test {

#include "fatal_error_to_exception.hpp"

#include "buffer.hpp"

static_assert(std::is_same_v<decltype(std::declval<const Buffer<int32_t>&>().raw()), int32_t*>);
static_assert(std::is_same_v<decltype(std::declval<const Buffer<int32_t>&>().shareRaw()), int32_t*>);
static_assert(!std::is_convertible_v<Buffer<>, uint8_t*>);
static_assert(!std::is_convertible_v<Buffer<>, Buffer<uint32_t>>);
static_assert(!std::is_convertible_v<void*, Buffer<>>);

struct SmartPointerValue
{
    int32_t value;
    int32_t doubled() const { return value * 2; }
};

TEST(BufferTest, DereferenceAndArrowAccessTypedObject)
{
    Buffer<SmartPointerValue> buffer(sizeof(SmartPointerValue));
    auto* value = ::new (buffer.raw<void*>()) SmartPointerValue{21};

    EXPECT_EQ(&*buffer, value);
    EXPECT_EQ(buffer->value, 21);
    EXPECT_EQ(buffer->doubled(), 42);
    (*buffer).value = 10;
    EXPECT_EQ(buffer->value, 10);

    const auto& constBuffer = buffer;
    EXPECT_EQ(constBuffer->value, 10);
}

// Tests default construction. It should allocate a non-null empty buffer
// aligned to 64 bytes; size and pointer alignment are checked directly.
TEST(BufferTest, DefaultConstructionAllocatesAlignedEmptyBuffer)
{
    Buffer<> buffer;
    auto* data = buffer.raw<uint8_t*>();

    ASSERT_NE(data, nullptr);
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(data) % uintptr_t{64}, uintptr_t{0});
}

TEST(BufferTest, AllocatedSizeReportsByteCapacityAndTracksDetachment)
{
    Buffer<uint32_t> buffer(8, 128);
    const auto& constBuffer = buffer;
    int32_t capacity = constBuffer.allocatedSize();
    EXPECT_GE(capacity, 136);
    EXPECT_EQ(capacity, RawBuffer::allocatedSize(buffer.raw<uint8_t*>()));

    Buffer<uint32_t> shared(buffer);
    EXPECT_EQ(shared.allocatedSize(), capacity);
    buffer.size(capacity + 1);
    EXPECT_GE(buffer.allocatedSize(), capacity + 1);
    EXPECT_EQ(shared.allocatedSize(), capacity);
    EXPECT_EQ(shared.size(), 8);
}

// Tests raw pointer access. The buffer should convert to the requested
// pointer type without changing the underlying memory address.
TEST(BufferTest, RawReturnsRequestedPointerType)
{
    Buffer<> buffer(8);
    auto* data = buffer.raw<uint8_t*>();
    data[0] = 0x10;
    data[1] = 0x20;
    data[2] = 0x30;
    data[3] = 0x40;

    EXPECT_EQ(buffer.raw<uint8_t*>(), data);
    EXPECT_EQ(buffer.raw<int32_t*>(), reinterpret_cast<int32_t*>(data));
}

// Tests zero-size construction and later resizing. Zero-size operations should
// remain valid, and growing to four bytes should make writable storage usable.
TEST(BufferTest, ZeroSizedConstructionAndResizeRemainUsable)
{
    Buffer<> buffer(0);
    auto* originalData = buffer.raw<uint8_t*>();

    buffer.size(0);
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.raw<uint8_t*>(), originalData);

    buffer.size(4);
    EXPECT_EQ(buffer.size(), 4);
    buffer.raw<uint8_t*>()[0] = 0xA5;
    EXPECT_EQ(buffer.raw<uint8_t*>()[0], 0xA5);

    buffer.size(0);
    EXPECT_EQ(buffer.size(), 0);
}

// Tests the maximum allocation limit. Requests exceeding it, including an
// overflowing size-plus-preallocation sum, should throw FatalErrorException.
TEST(BufferTest, AllocationRequestsAboveMaximumCapacityThrowFatalError)
{
    constexpr int32_t maximumCapacity = 1024 * 1024 * 1024;

    EXPECT_THROW({ Buffer<> tooLarge(maximumCapacity + 1); }, FatalErrorException);
    EXPECT_THROW({ Buffer<> tooMuchPreallocation(0, maximumCapacity + 1); }, FatalErrorException);
    EXPECT_THROW({ Buffer<> overflowingCapacity(1, maximumCapacity); }, FatalErrorException);
}

// Tests growth within reserved capacity. The logical size should increase
// without changing the data pointer or the bytes already stored.
TEST(BufferTest, PreallocationAllowsGrowthWithoutReplacingAllocation)
{
    Buffer<> buffer(4, 256);
    auto* originalData = buffer.raw<uint8_t*>();
    originalData[0] = 0xA5;
    originalData[1] = 0x5A;

    buffer.size(200);

    EXPECT_EQ(buffer.size(), 200);
    EXPECT_EQ(buffer.raw<uint8_t*>(), originalData);
    EXPECT_THAT(std::vector<uint8_t>(originalData, originalData + 2),
                ::testing::ElementsAre(0xA5, 0x5A));
}

// Tests grow() allocating space beyond the original buffer. It should update
// the logical size and preserve all previous bytes.
TEST(BufferTest, GrowPreservesExistingContent)
{
    Buffer<> buffer(3);
    auto* data = buffer.raw<uint8_t*>();
    data[0] = 0x12;
    data[1] = 0x34;
    data[2] = 0x56;

    buffer.grow(12);

    EXPECT_EQ(buffer.size(), 12);
    auto* grownData = buffer.raw<uint8_t*>();
    EXPECT_THAT(std::vector<uint8_t>(grownData, grownData + 3),
                ::testing::ElementsAre(0x12, 0x34, 0x56));
}

// Tests grow() after shrinking. grow() should retain capacity so regrowing to
// the former size reuses the allocation and leaves both end bytes intact.
TEST(BufferTest, GrowRetainsCapacityWhenShrinkingAndGrowingAgain)
{
    Buffer<> buffer;
    buffer.grow(100);
    auto* originalData = buffer.raw<uint8_t*>();
    originalData[0] = 0x12;
    originalData[99] = 0x56;

    buffer.grow(20);
    EXPECT_EQ(buffer.size(), 20);
    EXPECT_EQ(buffer.raw<uint8_t*>(), originalData);

    buffer.grow(100);
    EXPECT_EQ(buffer.size(), 100);
    EXPECT_EQ(buffer.raw<uint8_t*>(), originalData);
    EXPECT_EQ(originalData[0], 0x12);
    EXPECT_EQ(originalData[99], 0x56);
}

// Tests size() truncation and regrowth inside reserved capacity. The pointer
// should remain stable and all bytes retained by the truncation should survive.
TEST(BufferTest, SizeCanTruncateAndRegrowWithinPreallocatedCapacity)
{
    Buffer<> buffer(16, 64);
    auto* originalData = buffer.raw<uint8_t*>();
    for (int32_t i = 0; i < buffer.size(); ++i) {
        originalData[i] = static_cast<uint8_t>(i + 1);
    }

    buffer.size(8, true, 0, true);
    EXPECT_EQ(buffer.size(), 8);
    EXPECT_EQ(buffer.raw<uint8_t*>(), originalData);

    buffer.size(32, true, 0, true);
    EXPECT_EQ(buffer.size(), 32);
    EXPECT_EQ(buffer.raw<uint8_t*>(), originalData);
    EXPECT_THAT(std::vector<uint8_t>(originalData, originalData + 8),
                ::testing::ElementsAre(1, 2, 3, 4, 5, 6, 7, 8));
}

// Tests shrinking a large buffer. The logical size should decrease while bytes
// within the new size remain unchanged.
TEST(BufferTest, ShrinkingPreservesContentWithinNewLogicalSize)
{
    Buffer<> buffer(1024, 1024);
    auto* data = buffer.raw<uint8_t*>();
    data[0] = 0xA5;
    data[1] = 0x5A;

    buffer.size(1);

    EXPECT_EQ(buffer.size(), 1);
    EXPECT_EQ(buffer.raw<uint8_t*>()[0], 0xA5);
}

// Tests a shrink whose explicit preallocation keeps it within the reuse
// threshold. The allocation pointer and existing byte should remain unchanged.
TEST(BufferTest, ExplicitPreallocationCanAvoidShrinkingReallocation)
{
    Buffer<> buffer(64, 512);
    auto* originalData = buffer.raw<uint8_t*>();
    originalData[0] = 0xA5;

    buffer.size(1, true, 512);

    EXPECT_EQ(buffer.size(), 1);
    EXPECT_EQ(buffer.raw<uint8_t*>(), originalData);
    EXPECT_EQ(originalData[0], 0xA5);
}

// Tests resizing a shared buffer with content copying disabled. The resized
// instance should detach, while the other copy keeps its original bytes/size.
TEST(BufferTest, ResizingWithoutKeepingContentDoesNotChangeOtherCopies)
{
    Buffer<> original(4);
    auto* originalData = original.raw<uint8_t*>();
    originalData[0] = 0x12;
    originalData[1] = 0x34;
    originalData[2] = 0x56;
    originalData[3] = 0x78;
    Buffer<> copy(original);

    original.size(8, false);

    EXPECT_EQ(original.size(), 8);
    EXPECT_NE(original.raw<uint8_t*>(), originalData);
    EXPECT_EQ(copy.size(), 4);
    EXPECT_EQ(copy.raw<uint8_t*>(), originalData);
    EXPECT_THAT(std::vector<uint8_t>(originalData, originalData + 4),
                ::testing::ElementsAre(0x12, 0x34, 0x56, 0x78));
}

// Tests copy construction and same-size resize. Copies should share storage,
// and requesting the current size should leave that shared allocation intact.
TEST(BufferTest, CopySharesAllocationUntilResizeDetaches)
{
    Buffer<> original(3);
    auto* originalData = original.raw<uint8_t*>();
    originalData[0] = 0x12;
    originalData[1] = 0x34;
    originalData[2] = 0x56;

    Buffer<> copy(original);
    EXPECT_EQ(copy.raw<uint8_t*>(), originalData);

    original.size(3, false, 128);
    EXPECT_EQ(original.raw<uint8_t*>(), originalData);
    EXPECT_EQ(copy.raw<uint8_t*>(), originalData);

    original.size(8);

    auto* resizedData = original.raw<uint8_t*>();
    EXPECT_NE(resizedData, originalData);
    EXPECT_EQ(original.size(), 8);
    EXPECT_EQ(copy.size(), 3);
    EXPECT_THAT(std::vector<uint8_t>(resizedData, resizedData + 3),
                ::testing::ElementsAre(0x12, 0x34, 0x56));
    EXPECT_THAT(std::vector<uint8_t>(copy.raw<uint8_t*>(),
                                     copy.raw<uint8_t*>() + 3),
                ::testing::ElementsAre(0x12, 0x34, 0x56));
}

// Tests detachment when one of several copies changes size. Only the resized
// copy should receive independent storage; the other copies should retain data.
TEST(BufferTest, MultipleCopiesDetachIndependently)
{
    Buffer<> original(2);
    auto* data = original.raw<uint8_t*>();
    data[0] = 0x12;
    data[1] = 0x34;
    Buffer<> firstCopy(original);
    Buffer<> secondCopy(firstCopy);

    firstCopy.size(5);
    firstCopy.raw<uint8_t*>()[0] = 0xAB;

    EXPECT_EQ(original.size(), 2);
    EXPECT_EQ(secondCopy.size(), 2);
    EXPECT_EQ(original.raw<uint8_t*>(), data);
    EXPECT_EQ(secondCopy.raw<uint8_t*>(), data);
    EXPECT_THAT(std::vector<uint8_t>(data, data + 2),
                ::testing::ElementsAre(0x12, 0x34));
    EXPECT_EQ(firstCopy.raw<uint8_t*>()[0], 0xAB);
}

// Tests constructing a Buffer from another buffer's data pointer. It should
// share the allocation, then detach on resize without changing the original.
TEST(BufferTest, ConstructingFromDataPointerSharesAllocation)
{
    Buffer<> original(2);
    auto* data = original.raw<uint8_t*>();
    data[0] = 0x12;
    data[1] = 0x34;

    Buffer<> reference(static_cast<void*>(data));

    EXPECT_EQ(reference.raw<uint8_t*>(), data);
    EXPECT_EQ(reference.size(), 2);
    reference.raw<uint8_t*>()[1] = 0xAB;
    EXPECT_EQ(data[1], 0xAB);

    reference.size(4);
    EXPECT_EQ(original.size(), 2);
    EXPECT_NE(reference.raw<uint8_t*>(), data);
    EXPECT_EQ(original.raw<uint8_t*>(), data);
    EXPECT_EQ(data[1], 0xAB);
}

// Tests copy assignment over an existing allocation. The assigned buffer
// should share writes initially, then detach on resize and preserve the source.
TEST(BufferTest, CopyAssignmentSharesAllocation)
{
    Buffer<> original(2);
    auto* originalData = original.raw<uint8_t*>();
    originalData[0] = 0x12;

    Buffer<> assigned;
    assigned = original;

    EXPECT_EQ(assigned.raw<uint8_t*>(), originalData);
    EXPECT_EQ(assigned.size(), original.size());
    assigned.raw<uint8_t*>()[0] = 0xAB;
    EXPECT_EQ(originalData[0], 0xAB);

    assigned.size(4);
    EXPECT_NE(assigned.raw<uint8_t*>(), originalData);
    EXPECT_EQ(original.size(), 2);
    EXPECT_EQ(originalData[0], 0xAB);
}

// Tests self-copy assignment. It should preserve the allocation, logical size,
// and stored bytes; pointer and byte comparisons verify that behavior.
TEST(BufferTest, SelfCopyAssignmentPreservesBuffer)
{
    Buffer<> buffer(3);
    auto* data = buffer.raw<uint8_t*>();
    data[0] = 0x12;
    data[1] = 0x34;
    data[2] = 0x56;

    buffer = buffer;

    EXPECT_EQ(buffer.size(), 3);
    EXPECT_EQ(buffer.raw<uint8_t*>(), data);
    EXPECT_THAT(std::vector<uint8_t>(data, data + 3),
                ::testing::ElementsAre(0x12, 0x34, 0x56));
}

// Tests reusing moved-from objects through copy and move assignment. Assignment
// should restore a valid allocation without affecting the remaining owner.
TEST(BufferTest, MovedFromBufferCanBeAssignedAgain)
{
    Buffer<> source(2);
    auto* sourceData = source.raw<uint8_t*>();
    Buffer<> movedFrom(std::move(source));

    source = movedFrom;
    EXPECT_EQ(source.raw<uint8_t*>(), sourceData);
    EXPECT_EQ(source.size(), 2);

    Buffer<> replacement(1);
    auto* replacementData = replacement.raw<uint8_t*>();
    movedFrom = std::move(replacement);

    EXPECT_EQ(movedFrom.raw<uint8_t*>(), replacementData);
    EXPECT_EQ(movedFrom.size(), 1);
    EXPECT_EQ(replacement.raw<uint8_t*>(), nullptr);
    EXPECT_EQ(source.raw<uint8_t*>(), sourceData);
    EXPECT_EQ(source.size(), 2);
}

// Tests move construction followed by move assignment. Each operation should
// transfer the original allocation to its destination.
TEST(BufferTest, MoveConstructionAndAssignmentTransferAllocation)
{
    Buffer<> source(2);
    auto* sourceData = source.raw<uint8_t*>();

    Buffer<> moved(std::move(source));
    EXPECT_EQ(moved.raw<uint8_t*>(), sourceData);
    EXPECT_EQ(source.raw<uint8_t*>(), nullptr);

    Buffer<> destination(1);
    destination = std::move(moved);

    EXPECT_EQ(destination.raw<uint8_t*>(), sourceData);
    EXPECT_EQ(destination.size(), 2);
}

// Tests move-assigning over a buffer that has another owner. The destination
// should take the source allocation while the old allocation's copy stays live.
TEST(BufferTest, MoveAssignmentReleasesOnlyItsReferenceToSharedOldBuffer)
{
    Buffer<> oldBuffer(1);
    auto* oldData = oldBuffer.raw<uint8_t*>();
    oldData[0] = 0xA5;
    Buffer<> oldBufferCopy(oldBuffer);

    Buffer<> source(2);
    auto* sourceData = source.raw<uint8_t*>();
    sourceData[0] = 0x12;
    sourceData[1] = 0x34;

    oldBuffer = std::move(source);

    EXPECT_EQ(oldBuffer.raw<uint8_t*>(), sourceData);
    EXPECT_EQ(source.raw<uint8_t*>(), nullptr);
    EXPECT_EQ(oldBuffer.size(), 2);
    EXPECT_EQ(oldBufferCopy.size(), 1);
    EXPECT_EQ(oldBufferCopy.raw<uint8_t*>(), oldData);
    EXPECT_EQ(oldData[0], 0xA5);
}

// Tests self-move assignment and self-swap. Both should be no-ops, verified by
// checking that the original allocation, size, and data remain unchanged.
TEST(BufferTest, SelfMoveAssignmentAndSelfSwapAreNoOps)
{
    Buffer<> buffer(2);
    auto* data = buffer.raw<uint8_t*>();
    data[0] = 0x12;

    Buffer<>* self = &buffer;
    buffer = std::move(*self);
    buffer.swap(buffer);

    EXPECT_EQ(buffer.size(), 2);
    EXPECT_EQ(buffer.raw<uint8_t*>(), data);
    EXPECT_EQ(data[0], 0x12);
}

// Tests swapping two buffers. Each object should receive the other's pointer
// and size.
TEST(BufferTest, SwapExchangesAllocations)
{
    Buffer<> first(2);
    Buffer<> second(5);
    auto* firstData = first.raw<uint8_t*>();
    auto* secondData = second.raw<uint8_t*>();

    first.swap(second);

    EXPECT_EQ(first.raw<uint8_t*>(), secondData);
    EXPECT_EQ(second.raw<uint8_t*>(), firstData);
    EXPECT_EQ(first.size(), 5);
    EXPECT_EQ(second.size(), 2);
}

// Tests the documented const conversion behavior. A pointer obtained from a
// const Buffer remains mutable and writes through to its underlying storage.
TEST(BufferTest, ConstBufferStillProvidesMutableDataPointer)
{
    Buffer<> buffer(1);
    const Buffer<>& constBuffer = buffer;
    auto* data = constBuffer.raw<uint8_t*>();

    data[0] = 0xA5;

    EXPECT_EQ(buffer.raw<uint8_t*>()[0], 0xA5);
}

// Tests all supported typed raw pointers. They should refer to the same
// storage, whose 64-byte alignment satisfies each converted pointer type.
TEST(BufferTest, TypedPointerConversionsReferToSameAlignedStorage)
{
    Buffer<> buffer(16);
    auto* data = buffer.raw<uint8_t*>();

    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(buffer.raw<int8_t*>()));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(buffer.raw<int16_t*>()));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(buffer.raw<uint16_t*>()));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(buffer.raw<int32_t*>()));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(buffer.raw<uint32_t*>()));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(buffer.raw<int64_t*>()));
    EXPECT_EQ(static_cast<void*>(data), static_cast<void*>(buffer.raw<uint64_t*>()));
    EXPECT_EQ(static_cast<void*>(data), buffer.raw<void*>());
    EXPECT_EQ(reinterpret_cast<uintptr_t>(data) % uintptr_t{64}, uintptr_t{0});
}

TEST(BufferTest, TypedBufferUsesByteSizesAndPreallocation)
{
    Buffer<uint32_t> buffer(8, 64);
    auto* words = buffer.raw<uint32_t*>();
    auto* bytes = buffer.raw<uint8_t*>();
    ASSERT_NE(words, nullptr);
    EXPECT_EQ(static_cast<void*>(words), static_cast<void*>(bytes));
    EXPECT_EQ(buffer.size(), 8);
    bytes[0] = 0x12;
    bytes[7] = 0x34;

    buffer.size(12);
    EXPECT_EQ(buffer.size(), 12);
    EXPECT_EQ(buffer.raw<uint32_t*>(), words);
    EXPECT_EQ(bytes[0], 0x12);
    EXPECT_EQ(bytes[7], 0x34);

    buffer.grow(4);
    EXPECT_EQ(buffer.size(), 4);
    EXPECT_EQ(buffer.raw<uint8_t*>()[0], 0x12);

    EXPECT_THROW({ Buffer<uint32_t> tooLarge(0, RawBuffer::maximumCapacity + 1); }, FatalErrorException);
}

TEST(BufferTest, RawBorrowsWhileShareRawAddsAnOwnedReference)
{
    Buffer<uint32_t> buffer(8);
    auto* bytes = buffer.raw<uint8_t*>();
    ASSERT_EQ(RawBuffer::refs(bytes), 1);
    EXPECT_EQ(static_cast<void*>(buffer.raw<uint32_t*>()), static_cast<void*>(bytes));
    EXPECT_EQ(RawBuffer::refs(bytes), 1);

    auto* shared = buffer.shareRaw<uint8_t*>();
    EXPECT_EQ(shared, bytes);
    EXPECT_EQ(RawBuffer::refs(bytes), 2);
    RawBuffer::dec(shared);
    EXPECT_EQ(RawBuffer::refs(bytes), 1);
}

TEST(BufferTest, SharedRawReferenceOutlivesBuffer)
{
    uint8_t* shared = nullptr;
    {
        Buffer<> buffer(1);
        buffer.raw<uint8_t*>()[0] = 0xA5;
        shared = buffer.shareRaw<uint8_t*>();
        EXPECT_EQ(RawBuffer::refs(shared), 2);
    }
    EXPECT_EQ(RawBuffer::refs(shared), 1);
    EXPECT_EQ(shared[0], 0xA5);
    RawBuffer::dec(shared);
}

TEST(BufferTest, ExplicitCastSharesAllocationAndResizeDetaches)
{
    Buffer<> bytes(8, 64);
    auto* original = bytes.raw<uint8_t*>();
    original[0] = 0x12;
    original[7] = 0x34;

    auto words = static_cast<Buffer<uint32_t>>(bytes);
    EXPECT_EQ(static_cast<void*>(words.raw<uint32_t*>()), static_cast<void*>(original));
    EXPECT_EQ(words.size(), 8);
    EXPECT_EQ(RawBuffer::refs(original), 2);

    words.size(12);
    auto* detached = words.raw<uint8_t*>();
    ASSERT_NE(detached, original);
    EXPECT_EQ(words.size(), 12);
    EXPECT_EQ(bytes.size(), 8);
    EXPECT_EQ(RawBuffer::refs(original), 1);
    EXPECT_EQ(RawBuffer::refs(detached), 1);
    EXPECT_EQ(detached[0], 0x12);
    EXPECT_EQ(detached[7], 0x34);
    detached[0] = 0xAB;
    EXPECT_EQ(original[0], 0x12);
}

TEST(BufferTest, VoidPointerAssignmentSharesAndHandlesSelfAlias)
{
    Buffer<> source(4);
    auto* data = source.raw<uint8_t*>();
    data[0] = 0x5A;
    Buffer<> destination(1);

    destination = static_cast<void*>(data);
    EXPECT_EQ(destination.raw<uint8_t*>(), data);
    EXPECT_EQ(destination.size(), 4);
    EXPECT_EQ(RawBuffer::refs(data), 2);

    destination = destination.raw<void*>();
    EXPECT_EQ(destination.raw<uint8_t*>(), data);
    EXPECT_EQ(RawBuffer::refs(data), 2);
    EXPECT_EQ(data[0], 0x5A);

    source = Buffer<>(1);
    ASSERT_EQ(RawBuffer::refs(data), 1);
    destination = destination.raw<void*>();
    EXPECT_EQ(destination.raw<uint8_t*>(), data);
    EXPECT_EQ(RawBuffer::refs(data), 1);
    EXPECT_EQ(data[0], 0x5A);
}

TEST(BufferTest, CopyMoveAndSelfAssignmentsMaintainReferenceCounts)
{
    Buffer<uint16_t> source(6);
    auto* data = source.raw<uint8_t*>();
    data[0] = 0xA5;
    Buffer<uint16_t> copy(source);
    EXPECT_EQ(RawBuffer::refs(data), 2);

    Buffer<uint16_t> assigned;
    assigned = source;
    EXPECT_EQ(RawBuffer::refs(data), 3);
    assigned = assigned;
    EXPECT_EQ(RawBuffer::refs(data), 3);

    Buffer<uint16_t> moved(std::move(copy));
    EXPECT_EQ(copy.raw<uint16_t*>(), nullptr);
    EXPECT_EQ(RawBuffer::refs(data), 3);
    auto* self = &moved;
    moved = std::move(*self);
    EXPECT_EQ(RawBuffer::refs(data), 3);
    copy = std::move(moved);
    EXPECT_EQ(moved.raw<uint16_t*>(), nullptr);
    EXPECT_EQ(copy.raw<uint8_t*>(), data);
    EXPECT_EQ(RawBuffer::refs(data), 3);
    EXPECT_EQ(copy.size(), 6);
    EXPECT_EQ(data[0], 0xA5);
}

}
