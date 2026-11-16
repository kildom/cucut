#include "includes.hpp"

namespace rawbuffer_test {

#include "fatal_error_to_exception.hpp"

#include "rawbuffer.hpp"

TEST(RawBufferTest, ExposesBufferLimits)
{
    EXPECT_EQ(RawBuffer::alignment, 64);
    EXPECT_EQ(RawBuffer::maximumCapacity, 1024 * 1024 * 1024);
}

TEST(RawBufferTest, AllocatesAlignedBufferWithOneReference)
{
    auto* buffer = RawBuffer::alloc(8, 16);

    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(buffer) % uintptr_t{64}, uintptr_t{0});
    EXPECT_EQ(RawBuffer::refs(buffer), 1);

    RawBuffer::dec(buffer);
}

TEST(RawBufferTest, ZeroSizedAllocationCanBeResized)
{
    auto* buffer = RawBuffer::alloc(0, 0);

    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(buffer) % uintptr_t{64}, uintptr_t{0});
    EXPECT_EQ(RawBuffer::refs(buffer), 1);
    EXPECT_EQ(RawBuffer::size(buffer), 0);

    auto* resized = RawBuffer::realloc(buffer, 1, 0, true, true);
    EXPECT_EQ(RawBuffer::size(resized), 1);
    resized[0] = 0x7B;
    EXPECT_EQ(resized[0], 0x7B);
    EXPECT_EQ(RawBuffer::refs(resized), 1);

    RawBuffer::dec(resized);
}

TEST(RawBufferTest, AllocatedSizeTracksCapacityAcrossResize)
{
    auto* buffer = RawBuffer::alloc(8, 128);
    int32_t capacity = RawBuffer::allocatedSize(buffer);
    EXPECT_GE(capacity, 136);
    EXPECT_LT(capacity, 136 + RawBuffer::alignment);

    auto* resized = RawBuffer::realloc(buffer, capacity, 0, true, true);
    EXPECT_EQ(resized, buffer);
    EXPECT_EQ(RawBuffer::allocatedSize(resized), capacity);

    resized = RawBuffer::realloc(resized, capacity + 1, 0, true, true);
    EXPECT_GE(RawBuffer::allocatedSize(resized), capacity + 1);
    EXPECT_EQ(RawBuffer::size(resized), capacity + 1);
    RawBuffer::dec(resized);
}

TEST(RawBufferTest, ReferenceCountTracksMultipleOwners)
{
    auto* buffer = RawBuffer::alloc(4, 0);

    RawBuffer::inc(buffer);
    RawBuffer::inc(buffer);
    EXPECT_EQ(RawBuffer::refs(buffer), 3);

    RawBuffer::dec(buffer);
    EXPECT_EQ(RawBuffer::refs(buffer), 2);
    RawBuffer::dec(buffer);
    EXPECT_EQ(RawBuffer::refs(buffer), 1);
    RawBuffer::dec(buffer);
}

TEST(RawBufferTest, SameSizeReallocKeepsSharedAllocation)
{
    auto* buffer = RawBuffer::alloc(4, 0);
    RawBuffer::inc(buffer);

    auto* resized = RawBuffer::realloc(buffer, 4, 128, false, false);

    EXPECT_EQ(resized, buffer);
    EXPECT_EQ(RawBuffer::refs(buffer), 2);

    RawBuffer::dec(buffer);
    RawBuffer::dec(resized);
}

TEST(RawBufferTest, ReallocRetainsCapacityAndContent)
{
    auto* buffer = RawBuffer::alloc(4, 32);
    buffer[0] = 0x12;
    buffer[1] = 0x34;

    auto* resized = RawBuffer::realloc(buffer, 20, 0, true, true);

    EXPECT_EQ(resized, buffer);
    EXPECT_EQ(resized[0], 0x12);
    EXPECT_EQ(resized[1], 0x34);
    EXPECT_EQ(RawBuffer::refs(resized), 1);

    RawBuffer::dec(resized);
}

TEST(RawBufferTest, ReallocGrowsBeyondCapacityAndPreservesContent)
{
    auto* buffer = RawBuffer::alloc(4, 0);
    buffer[0] = 0xC3;

    auto* resized = RawBuffer::realloc(buffer, 1024, 0, true, true);
    resized[1023] = 0x3C;

    EXPECT_EQ(resized[0], 0xC3);
    EXPECT_EQ(resized[1023], 0x3C);
    EXPECT_EQ(RawBuffer::refs(resized), 1);

    RawBuffer::dec(resized);
}

TEST(RawBufferTest, ReallocWithoutCopyDoesNotChangeSharedOriginal)
{
    auto* original = RawBuffer::alloc(4, 0);
    original[0] = 0xD6;
    RawBuffer::inc(original);

    auto* resized = RawBuffer::realloc(original, 8, 0, true, false);

    EXPECT_NE(resized, original);
    EXPECT_EQ(original[0], 0xD6);
    EXPECT_EQ(RawBuffer::refs(original), 1);
    EXPECT_EQ(RawBuffer::refs(resized), 1);

    RawBuffer::dec(original);
    RawBuffer::dec(resized);
}

TEST(RawBufferTest, ReallocDetachesSharedBufferAndPreservesContent)
{
    auto* original = RawBuffer::alloc(4, 0);
    original[0] = 0xA5;
    RawBuffer::inc(original);

    auto* resized = RawBuffer::realloc(original, 8, 0, true, true);

    EXPECT_NE(resized, original);
    EXPECT_EQ(resized[0], 0xA5);
    EXPECT_EQ(RawBuffer::size(original), 4);
    EXPECT_EQ(RawBuffer::size(resized), 8);
    EXPECT_EQ(RawBuffer::refs(original), 1);
    EXPECT_EQ(RawBuffer::refs(resized), 1);

    RawBuffer::dec(original);
    RawBuffer::dec(resized);
}

TEST(RawBufferTest, UniqueReturnsSameBufferWhenAlreadyUnique)
{
    auto* buffer = RawBuffer::alloc(4, 0);

    EXPECT_EQ(RawBuffer::unique(buffer), buffer);
    EXPECT_EQ(RawBuffer::refs(buffer), 1);

    RawBuffer::dec(buffer);
}

TEST(RawBufferTest, UniqueClonesSharedBuffer)
{
    auto* original = RawBuffer::alloc(4, 0);
    original[0] = 0x5A;
    RawBuffer::inc(original);

    auto* clone = RawBuffer::unique(original);

    EXPECT_NE(clone, original);
    EXPECT_EQ(clone[0], 0x5A);
    EXPECT_EQ(RawBuffer::refs(original), 1);
    EXPECT_EQ(RawBuffer::refs(clone), 1);

    clone[0] = 0xA5;
    EXPECT_EQ(original[0], 0x5A);

    RawBuffer::dec(original);
    RawBuffer::dec(clone);
}

TEST(RawBufferTest, RejectsOversizedAllocationRequests)
{
    EXPECT_THROW(
        RawBuffer::alloc(RawBuffer::maximumCapacity + 1, 0),
        FatalErrorException);
    EXPECT_THROW(
        RawBuffer::alloc(0, RawBuffer::maximumCapacity + 1),
        FatalErrorException);
    EXPECT_THROW(
        RawBuffer::alloc(1, RawBuffer::maximumCapacity),
        FatalErrorException);

    auto* buffer = RawBuffer::alloc(4, 0);
    EXPECT_THROW(
        RawBuffer::realloc(buffer, RawBuffer::maximumCapacity + 1, 0, true, true),
        FatalErrorException);
    EXPECT_EQ(RawBuffer::refs(buffer), 1);
    RawBuffer::dec(buffer);
}

}
