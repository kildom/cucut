#include "includes.hpp"

namespace buffer_assert_test {

// Exercise Buffer assertions independently of the RawBuffer implementation.
#include "rawbuffer.hpp"

class AssertionException : public std::runtime_error
{
public:
    AssertionException() : std::runtime_error("Buffer assertion failed") {}
};

static void checkAssertion(bool condition)
{
    if (!condition) {
        throw AssertionException();
    }
}

#undef CUCUT_ASSERT
#define CUCUT_ASSERT(condition, ...) checkAssertion(condition)
#include "buffer.hpp"
#undef CUCUT_ASSERT

TEST(BufferAssertTest, RawPointerConstructionAndAssignmentRequireNonNull)
{
    EXPECT_THROW({ Buffer<> buffer(static_cast<void*>(nullptr)); }, AssertionException);

    Buffer<> buffer(1);
    auto* data = buffer.raw();
    EXPECT_THROW({ buffer = static_cast<void*>(nullptr); }, AssertionException);
    EXPECT_EQ(buffer.raw(), data);
    EXPECT_EQ(RawBuffer::refs(data), 1);
}

TEST(BufferAssertTest, CopyAndShareRequireLiveSource)
{
    Buffer<> source(1);
    Buffer<> owner(std::move(source));

    EXPECT_THROW({ Buffer<> copy(source); }, AssertionException);
    Buffer<> target(1);
    auto* data = target.raw();
    EXPECT_THROW({ target = source; }, AssertionException);
    EXPECT_EQ(target.raw(), data);
    EXPECT_EQ(RawBuffer::refs(data), 1);
    EXPECT_THROW({ source.shareRaw(); }, AssertionException);
}

}
