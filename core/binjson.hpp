#ifndef BINJSON_HPP
#define BINJSON_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <limits>
#include <memory>
#include <vector>
#include <map>

#include "buffer.hpp"

namespace BinJSON {

/**
 * @brief Identifies the kind of a binJSON value.
 *
 * The underlying values correspond to the type tags used by the binJSON
 * format.
 */
enum class BinJSONType : std::uint8_t {
    /** Null value. */
    NUL = 0,
    /** External byte buffer. */
    BINARY = 1,
    /** Boolean value. */
    BOOLEAN = 2,
    /** Floating-point number. */
    NUMBER = 3,
    /** UTF-8 string. */
    STRING = 4,
    /** Ordered sequence of values. */
    ARRAY = 5,
    /** String-keyed collection of values. */
    OBJECT = 6,
    /** Internal error value; it has no representation in the binJSON format. */
    ERROR = 7,
};

class ArrayValue;
class ObjectValue;
class NullValue;

/**
 * @brief Base class for values represented by the binJSON object model.
 *
 * Values can expose type-specific data through the accessor methods. An
 * accessor that does not apply to a derived type returns an empty/default
 * value. Indexing an unsupported or missing element yields a reusable null
 * value.
 */
class AnyValue: public std::enable_shared_from_this<AnyValue> {

private:
    std::shared_ptr<AnyValue> nullValue;

public:

    /** @brief Return the type of this value. */
    virtual BinJSONType getType() = 0;
    /** @brief Return whether this value is null. */
    virtual bool isNull() { return false; }
    /** @brief Return whether this value represents an internal error. */
    virtual bool isError() { return false; }
    /** @brief Return the error message, or an empty string when not an error. */
    virtual std::string& error() { static std::string empty; return empty; }
    /** @brief Return the byte buffer held by this value, if any. */
    virtual Buffer<> buffer() { return Buffer<>(0); }
    /** @brief Return the held buffer cast to the requested element type. */
    template<class T>
    Buffer<T> buffer() { return static_cast<Buffer<T>>(buffer()); }
    /** @brief Return the boolean value, or false when not boolean. */
    virtual bool boolean() { return false; }
    /** @brief Return the numeric value, or NaN when not numeric. */
    virtual double number() { return std::numeric_limits<double>::quiet_NaN(); }
    /** @brief Return the string held by this value, or an empty string. */
    virtual std::string& string() { static std::string empty; return empty; }
    /** @brief Return the number of elements or properties in this value. */
    virtual int length() { return 0; }
    /** @brief Resize this value when it supports resizing. */
    virtual void length(int size);
    /** @brief Access an array element; unsupported or missing elements are null. */
    virtual std::shared_ptr<AnyValue>& get(int index);
    /** @brief Access an object property; unsupported or missing properties are null. */
    virtual std::shared_ptr<AnyValue>& get(const std::string& name);
    /** @brief Destroy the value through a base-class pointer. */
    virtual ~AnyValue() {}

    /** @brief Access an array element using subscript syntax. */
    std::shared_ptr<AnyValue>& operator[](int index) { return get(index); }
    /** @brief Access an object property using subscript syntax. */
    std::shared_ptr<AnyValue>& operator[](const std::string& name) { return get(name); }

    /**
     * @brief Obtain this value as a concrete value type.
     * @tparam T The requested derived value type.
     * @return This value when its type matches @c T; otherwise a default
     *         constructed @c T.
     */
    template<class T>
    std::shared_ptr<T> to() {
        if (getType() != T::type) {
            return std::make_shared<T>();
        }
        return std::static_pointer_cast<T>(shared_from_this());
    }
};


/** @brief Represents the binJSON null value. */
class NullValue: public AnyValue {

public:
    /** @brief Type tag for null values. */
    static constexpr BinJSONType type = BinJSONType::NUL;
    /** @brief Return the null type tag. */
    BinJSONType getType() override { return type; }
    /** @brief Always returns true. */
    bool isNull() override { return true; }
};

inline void AnyValue::length(int) { }

inline std::shared_ptr<AnyValue>& AnyValue::get(int)
{
    if (nullValue == nullptr || !nullValue->isNull()) {
        nullValue = std::make_shared<NullValue>();
    }
    return nullValue;
}

inline std::shared_ptr<AnyValue>& AnyValue::get(const std::string&)
{
    if (nullValue == nullptr || !nullValue->isNull()) {
        nullValue = std::make_shared<NullValue>();
    }
    return nullValue;
}


/** @brief Represents a byte buffer referenced by a binJSON value. */
class BinaryValue: public AnyValue {

public:
    using AnyValue::buffer;

    /** @brief Type tag for buffer values. */
    static constexpr BinJSONType type = BinJSONType::BINARY;
    /** @brief Buffer held by this value. */
    Buffer<> value;
    /** @brief Construct an empty buffer value. */
    BinaryValue(): value() { }
    /** @brief Construct a buffer value sharing @p value. */
    BinaryValue(const Buffer<>& value): value(value) { }
    /** @brief Return the buffer type tag. */
    BinJSONType getType() override { return type; }
    /** @brief Return the held buffer. */
    Buffer<> buffer() override { return value; }
};

/** @brief Represents a boolean value. */
class BooleanValue: public AnyValue {

public:
    /** @brief Type tag for boolean values. */
    static constexpr BinJSONType type = BinJSONType::BOOLEAN;
    /** @brief Boolean value. */
    bool value;
    /** @brief Construct a false boolean value. */
    BooleanValue(): value(false) { }
    /** @brief Construct a boolean value. */
    BooleanValue(bool value): value(value) { }
    /** @brief Return the boolean type tag. */
    BinJSONType getType() override { return type; }
    /** @brief Return the held boolean. */
    bool boolean() override { return value; }
};

/** @brief Represents a floating-point number. */
class NumberValue: public AnyValue {

public:
    /** @brief Type tag for numeric values. */
    static constexpr BinJSONType type = BinJSONType::NUMBER;
    /** @brief Numeric value. */
    double value;
    /** @brief Construct a number initialized to zero. */
    NumberValue(): value() { }
    /** @brief Construct a number with the specified value. */
    NumberValue(double value): value(value) { }
    /** @brief Return the number type tag. */
    BinJSONType getType() override { return type; }
    /** @brief Return the held number. */
    double number() override { return value; }
};


/** @brief Represents a UTF-8 string. */
class StringValue: public AnyValue
{
public:
    /** @brief Type tag for string values. */
    static constexpr BinJSONType type = BinJSONType::STRING;
    /** @brief String contents. */
    std::string value;
    /** @brief Construct an empty string. */
    StringValue(): value() { }
    /** @brief Construct a string from @p length bytes of @p str. */
    StringValue(const char* str, size_t length): value(str, length) { }
    /** @brief Construct a string by copying @p value. */
    StringValue(const std::string& value): value(value) { }
    /** @brief Return the string type tag. */
    BinJSONType getType() override { return type; }
    /** @brief Return the held string. */
    std::string& string() override { return value; }
    /** @brief Return the length of the string. */
    int length() override { return value.length(); }
};

/** @brief Represents an indexed sequence of binJSON values. */
class ArrayValue: public AnyValue
{
public:
    /** @brief Type tag for array values. */
    static constexpr BinJSONType type = BinJSONType::ARRAY;
    /** @brief Values in this array, in index order. */
    std::vector<std::shared_ptr<AnyValue>> value;

    /** @brief Construct an empty array. */
    ArrayValue(): value() { }
    /** @brief Construct an array with @p size default-initialized entries. */
    ArrayValue(size_t size): value(size) { }
    /** @brief Construct an array by copying @p value. */
    ArrayValue(const std::vector<std::shared_ptr<AnyValue>> &value): value(value) { }
    /** @brief Construct an array by moving @p value. */
    ArrayValue(std::vector<std::shared_ptr<AnyValue>>&& value) : value(std::move(value)) { }

    /** @brief Return the array type tag. */
    BinJSONType getType() override { return type; }

    /** @brief Return the number of array entries. */
    int length() override { return static_cast<int>(value.size()); }

    /** @brief Resize the array to @p size entries. */
    void length(int size) override
    {
        value.resize(size);
    }

    /** @brief Access an array entry, returning null for an out-of-range index. */
    std::shared_ptr<AnyValue>& get(int index)
    {
        if (index < 0 || static_cast<size_t>(index) >= value.size()) {
            return AnyValue::get(index);
        }
        return value[index];
    }
};


/** @brief Represents a string-keyed collection of binJSON values. */
class ObjectValue: public AnyValue
{
public:
    /** @brief Type tag for object values. */
    static constexpr BinJSONType type = BinJSONType::OBJECT;
    /** @brief Object properties, keyed by their names. */
    std::map<std::string, std::shared_ptr<AnyValue>> value;

    /** @brief Construct an empty object. */
    ObjectValue(): value() { }
    /** @brief Construct an object by copying @p value. */
    ObjectValue(const std::map<std::string, std::shared_ptr<AnyValue>> &value): value(value) { }
    /** @brief Construct an object by moving @p value. */
    ObjectValue(std::map<std::string, std::shared_ptr<AnyValue>>&& value) : value(std::move(value)) { }

    /** @brief Return the object type tag. */
    BinJSONType getType() override { return type; }

    /** @brief Return the number of properties in the object. */
    int length() override { return static_cast<int>(value.size()); }

    /** @brief Access a property, creating it as null if it is missing. */
    std::shared_ptr<AnyValue>& get(const std::string& name) override
    {
        if (value.count(name) == 0) {
            value[name] = std::make_shared<NullValue>();
        }
        return value[name];
    }
};


/**
 * @brief Represents an internal error that cannot be encoded in binJSON.
 *
 * Generating binJSON from an error value encodes it as null.
 */
class ErrorValue: public AnyValue
{
public:
    /** @brief Type tag identifying this internal error value. */
    static constexpr BinJSONType type = BinJSONType::ERROR;
    /** @brief Error message. */
    std::string message;
    /** @brief Construct an error with an empty message. */
    ErrorValue(): message() { }
    /** @brief Construct an error from a null-terminated message. */
    ErrorValue(const char* str): message(str) { }
    /** @brief Construct an error by copying @p value. */
    ErrorValue(const std::string& value): message(value) { }
    /** @brief Return the error type tag. */
    BinJSONType getType() override { return type; }
    /** @brief Always returns true. */
    bool isError() override{ return true; }
    /** @brief Return the error message. */
    std::string& error() override{ return message; }
};

using Any = std::shared_ptr<AnyValue>;
using Null = std::shared_ptr<NullValue>;
using Binary = std::shared_ptr<BinaryValue>;
using Boolean = std::shared_ptr<BooleanValue>;
using Number = std::shared_ptr<NumberValue>;
using String = std::shared_ptr<StringValue>;
using Array = std::shared_ptr<ArrayValue>;
using Object = std::shared_ptr<ObjectValue>;
using Error = std::shared_ptr<ErrorValue>;

template<typename... Args>
inline auto makeAny(Args&&... args) {
    return std::make_shared<AnyValue>(std::forward<Args>(args)...);
}

template<typename... Args>
inline auto makeNull(Args&&... args) {
    return std::make_shared<NullValue>(std::forward<Args>(args)...);
}

template<typename... Args>
inline auto makeBinary(Args&&... args) {
    return std::make_shared<BinaryValue>(std::forward<Args>(args)...);
}

template<typename... Args>
inline auto makeBoolean(Args&&... args) {
    return std::make_shared<BooleanValue>(std::forward<Args>(args)...);
}

template<typename... Args>
inline auto makeNumber(Args&&... args) {
    return std::make_shared<NumberValue>(std::forward<Args>(args)...);
}

template<typename... Args>
inline auto makeString(Args&&... args) {
    return std::make_shared<StringValue>(std::forward<Args>(args)...);
}

template<typename... Args>
inline auto makeArray(Args&&... args) {
    return std::make_shared<ArrayValue>(std::forward<Args>(args)...);
}

template<typename... Args>
inline auto makeObject(Args&&... args) {
    return std::make_shared<ObjectValue>(std::forward<Args>(args)...);
}

template<typename... Args>
inline auto makeError(Args&&... args) {
    return std::make_shared<ErrorValue>(std::forward<Args>(args)...);
}


/** @brief Generate binJSON from Value into buffers.
 *
 * @param value        Value to convert to binJSON.
 * @param buffer       Buffer where to encode binJSON.
 * @param bufferOffset Offset in the buffer where start to write.
 * @param array        Array of buffers where to store binary values from value.
 * @param arrayOffset  Element index in array where to start putting buffers.
 * @param bufferIndex  Index in binJSON from which to start indexing binary value.
 */
void generate(const Any& value, Buffer<>& buffer, int32_t bufferOffset,
    std::vector<Buffer<>>& array, int32_t arrayOffset, int32_t bufferIndex);

static inline void generate(const Any& value, std::vector<Buffer<>>& array)
{
    array.resize(1);
    Buffer<> buffer(0);
    generate(value, buffer, 0, array, 1, 1);
    array[0] = buffer;
}

/** @brief Parse binJSON and convert to AnyValue.
 *
 * @param buffer       Buffer containing binJSON data.
 * @param bufferOffset Offset in buffer where binJSON data starts.
 * @param array        Array of binary values that should be used when parsing.
 * @param arrayOffset  Where referenced binary values starts in the array.
 * @param bufferIndex  What is the starting index in binJSON of the binary value.
 * @returns            Parsed top-level value or BinJSON::ErrorValue if a parsing error occurs.
 */
Any parse(Buffer<> buffer, int32_t bufferOffset,
    const std::vector<Buffer<>>& array, int32_t arrayOffset, int32_t bufferIndex);

/** @brief Parse binJSON using the default buffer offsets.
 *
 * @param array Buffers containing the binJSON data and referenced binary values.
 * @returns Parsed top-level value, or an BinJSON::ErrorValue if the input is empty or parsing fails.
 */
static inline Any parse(const std::vector<Buffer<>>& array)
{
    if (array.size() == 0) {
        return makeError("Empty input");
    }
    return parse(array[0], 0, array, 1, 1);
}

};

/** @brief Create a binJSON number from an unsigned integer literal. */
inline BinJSON::Number operator"" _binjson(unsigned long long value)
{
    return BinJSON::makeNumber((double)value);
}

/** @brief Create a binJSON number from a floating-point literal. */
inline BinJSON::Number operator"" _binjson(long double value)
{
    return BinJSON::makeNumber((double)value);
}

/** @brief Create a binJSON string from a string literal. */
inline BinJSON::String operator"" _binjson(const char* str, size_t length)
{
    return BinJSON::makeString(str, length);
}

inline BinJSON::Null binJSON(std::nullptr_t)
{
    return BinJSON::makeNull();
}

inline BinJSON::Boolean binJSON(bool value)
{
    return BinJSON::makeBoolean(value);
}

template<typename T, std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>, int> = 0>
BinJSON::Number binJSON(T value) {
    return BinJSON::makeNumber((double)value);
}

inline BinJSON::String binJSON(const char* text) {
    return BinJSON::makeString(text);
}

inline BinJSON::String binJSON(const std::string &text) {
    return BinJSON::makeString(text);
}

template<class T>
BinJSON::Binary binJSON(const Buffer<T>& buffer) {
    return BinJSON::makeBinary(static_cast<Buffer<>>(buffer));
}

inline BinJSON::Object binJSON(const std::map<std::string, BinJSON::Any> &value)
{
    return BinJSON::makeObject(value);
}

/** @brief Create an array value from an array-valued vector. */
template <size_t N>
BinJSON::Array binJSON(const std::array<BinJSON::Any, N> &value)
{
    return BinJSON::makeArray(std::vector<BinJSON::Any>(value.begin(), value.end()));
}

/** @brief Create an array value by copying @p value. */
inline BinJSON::Array binJSON(const std::vector<BinJSON::Any> &value)
{
    return BinJSON::makeArray(value);
}

template<class T>
BinJSON::Array binJSONArray(const T* array, int32_t size) {
    return binJSON(std::vector<BinJSON::Any>(array, array + size));
}

inline BinJSON::Object binJSONObject(std::initializer_list<std::pair<const std::string, BinJSON::Any>> values) {
    return binJSON(std::map<std::string, BinJSON::Any>(values));
}

#endif // BINJSON_HPP
