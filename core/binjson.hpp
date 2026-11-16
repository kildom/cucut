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
    /** Floating-point number. */
    NUMBER = 2,
    /** UTF-8 string. */
    STRING = 3,
    /** Ordered sequence of values. */
    ARRAY = 4,
    /** String-keyed collection of values. */
    OBJECT = 5,
    /** Internal error value; it has no representation in the binJSON format. */
    ERROR = 6,
};

class Array;
class Object;
class Null;

/**
 * @brief Base class for values represented by the binJSON object model.
 *
 * Values can expose type-specific data through the accessor methods. An
 * accessor that does not apply to a derived type returns an empty/default
 * value. Indexing an unsupported or missing element yields a reusable null
 * value.
 */
class Value: public std::enable_shared_from_this<Value> {

private:
    std::shared_ptr<Value> nullValue;

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
    virtual Buffer buffer() { return Buffer(0); }
    /** @brief Return the numeric value, or NaN when not numeric. */
    virtual double number() { return std::numeric_limits<double>::quiet_NaN(); }
    /** @brief Return the string held by this value, or an empty string. */
    virtual std::string& string() { static std::string empty; return empty; }
    /** @brief Return the number of elements or properties in this value. */
    virtual int length() { return 0; }
    /** @brief Resize this value when it supports resizing. */
    virtual void length(int size);
    /** @brief Access an array element; unsupported or missing elements are null. */
    virtual std::shared_ptr<Value>& get(int index);
    /** @brief Access an object property; unsupported or missing properties are null. */
    virtual std::shared_ptr<Value>& get(const std::string& name);
    /** @brief Destroy the value through a base-class pointer. */
    virtual ~Value() {}

    /** @brief Access an array element using subscript syntax. */
    std::shared_ptr<Value>& operator[](int index) { return get(index); }
    /** @brief Access an object property using subscript syntax. */
    std::shared_ptr<Value>& operator[](const std::string& name) { return get(name); }

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
class Null: public Value {

public:
    /** @brief Type tag for null values. */
    static constexpr BinJSONType type = BinJSONType::NUL;
    /** @brief Return the null type tag. */
    BinJSONType getType() override { return type; }
    /** @brief Always returns true. */
    bool isNull() override { return true; }
};

inline void Value::length(int) { }

inline std::shared_ptr<Value>& Value::get(int)
{
    if (nullValue == nullptr || !nullValue->isNull()) {
        nullValue = std::make_shared<Null>();
    }
    return nullValue;
}

inline std::shared_ptr<Value>& Value::get(const std::string&)
{
    if (nullValue == nullptr || !nullValue->isNull()) {
        nullValue = std::make_shared<Null>();
    }
    return nullValue;
}


/** @brief Represents a byte buffer referenced by a binJSON value. */
class Binary: public Value {

public:
    /** @brief Type tag for buffer values. */
    static constexpr BinJSONType type = BinJSONType::BINARY;
    /** @brief Buffer held by this value. */
    Buffer value;
    /** @brief Construct an empty buffer value. */
    Binary(): value() { }
    /** @brief Construct a buffer value sharing @p value. */
    Binary(const Buffer& value): value(value) { }
    /** @brief Return the buffer type tag. */
    BinJSONType getType() override { return type; }
    /** @brief Return the held buffer. */
    Buffer buffer() override { return value; }
};

/** @brief Represents a floating-point number. */
class Number: public Value {

public:
    /** @brief Type tag for numeric values. */
    static constexpr BinJSONType type = BinJSONType::NUMBER;
    /** @brief Numeric value. */
    double value;
    /** @brief Construct a number initialized to zero. */
    Number(): value() { }
    /** @brief Construct a number with the specified value. */
    Number(double value): value(value) { }
    /** @brief Return the number type tag. */
    BinJSONType getType() override { return type; }
    /** @brief Return the held number. */
    double number() override { return value; }
};


/** @brief Represents a UTF-8 string. */
class String: public Value
{
public:
    /** @brief Type tag for string values. */
    static constexpr BinJSONType type = BinJSONType::STRING;
    /** @brief String contents. */
    std::string value;
    /** @brief Construct an empty string. */
    String(): value() { }
    /** @brief Construct a string from @p length bytes of @p str. */
    String(const char* str, size_t length): value(str, length) { }
    /** @brief Construct a string by copying @p value. */
    String(const std::string& value): value(value) { }
    /** @brief Return the string type tag. */
    BinJSONType getType() override { return type; }
    /** @brief Return the held string. */
    std::string& string() override { return value; }
    /** @brief Return the length of the string. */
    int length() override { return value.length(); }
};

/** @brief Represents an indexed sequence of binJSON values. */
class Array: public Value
{
public:
    /** @brief Type tag for array values. */
    static constexpr BinJSONType type = BinJSONType::ARRAY;
    /** @brief Values in this array, in index order. */
    std::vector<std::shared_ptr<Value>> value;

    /** @brief Construct an empty array. */
    Array(): value() { }
    /** @brief Construct an array with @p size default-initialized entries. */
    Array(size_t size): value(size) { }
    /** @brief Construct an array by copying @p value. */
    Array(const std::vector<std::shared_ptr<Value>> &value): value(value) { }
    /** @brief Construct an array by moving @p value. */
    Array(std::vector<std::shared_ptr<Value>>&& value) : value(std::move(value)) { }

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
    std::shared_ptr<Value>& get(int index)
    {
        if (index < 0 || static_cast<size_t>(index) >= value.size()) {
            return Value::get(index);
        }
        return value[index];
    }
};


/** @brief Represents a string-keyed collection of binJSON values. */
class Object: public Value
{
public:
    /** @brief Type tag for object values. */
    static constexpr BinJSONType type = BinJSONType::OBJECT;
    /** @brief Object properties, keyed by their names. */
    std::map<std::string, std::shared_ptr<Value>> value;

    /** @brief Construct an empty object. */
    Object(): value() { }
    /** @brief Construct an object by copying @p value. */
    Object(const std::map<std::string, std::shared_ptr<Value>> &value): value(value) { }
    /** @brief Construct an object by moving @p value. */
    Object(std::map<std::string, std::shared_ptr<Value>>&& value) : value(std::move(value)) { }

    /** @brief Return the object type tag. */
    BinJSONType getType() override { return type; }

    /** @brief Return the number of properties in the object. */
    int length() override { return static_cast<int>(value.size()); }

    /** @brief Access a property, creating it as null if it is missing. */
    std::shared_ptr<Value>& get(const std::string& name) override
    {
        if (value.count(name) == 0) {
            value[name] = std::make_shared<Null>();
        }
        return value[name];
    }
};


/**
 * @brief Represents an internal error that cannot be encoded in binJSON.
 *
 * Generating binJSON from an error value encodes it as null.
 */
class Error: public Value
{
public:
    /** @brief Type tag identifying this internal error value. */
    static constexpr BinJSONType type = BinJSONType::ERROR;
    /** @brief Error message. */
    std::string message;
    /** @brief Construct an error with an empty message. */
    Error(): message() { }
    /** @brief Construct an error from a null-terminated message. */
    Error(const char* str): message(str) { }
    /** @brief Construct an error by copying @p value. */
    Error(const std::string& value): message(value) { }
    /** @brief Return the error type tag. */
    BinJSONType getType() override { return type; }
    /** @brief Always returns true. */
    bool isError() override{ return true; }
    /** @brief Return the error message. */
    std::string& error() override{ return message; }
};


/** @brief Create a null value. */
static std::shared_ptr<Null> from(nullptr_t)
{
    return std::make_shared<Null>();
}

/** @brief Create a buffer value sharing @p value. */
static std::shared_ptr<Binary> from(const Buffer& value)
{
    return std::make_shared<Binary>(value);
}

/** @brief Create a number value. */
static std::shared_ptr<Number> from(double value)
{
    return std::make_shared<Number>(value);
}

/** @brief Create a string value. */
static std::shared_ptr<String> from(const std::string& value)
{
    return std::make_shared<String>(value);
}


/** @brief Create an array value by copying @p value. */
static std::shared_ptr<Array> from(const std::vector<std::shared_ptr<Value>> &value)
{
    return std::make_shared<Array>(value);
}

/** @brief Create an array value from an array-valued vector. */
template <size_t N>
static std::shared_ptr<Array> from(const std::vector<std::array<Value, N>> &value)
{
    return std::make_shared<Array>(value);
}


/** @brief Create an object value by copying @p value. */
static std::shared_ptr<Object> from(const std::map<std::string, std::shared_ptr<Value>> &value)
{
    return std::make_shared<Object>(value);
}

//std::shared_ptr<Value> parse(Buffer data, int32_t offset = 0, int32_t size = -1);

/** @brief Generate binJSON from Value into buffers.
 *
 * @param value        Value to convert to binJSON.
 * @param buffer       Buffer where to encode binJSON.
 * @param bufferOffset Offset in the buffer where start to write.
 * @param array        Array of buffers where to store binary values from value.
 * @param arrayOffset  Element index in array where to start putting buffers.
 * @param bufferIndex  Index in binJSON from which to start indexing binary value.
 */
void generate(const std::shared_ptr<Value>& value, Buffer& buffer, int32_t bufferOffset,
    std::vector<Buffer>& array, int32_t arrayOffset, int32_t bufferIndex);

static inline void generate(const std::shared_ptr<Value>& value, std::vector<Buffer>& array)
{
    array.resize(1);
    Buffer buffer(0);
    generate(value, buffer, 0, array, 1, 1);
    array[0] = buffer;
}

/** @brief Parse binJSON and convert to Value.
 *
 * @param buffer       Buffer containing binJSON data.
 * @param bufferOffset Offset in buffer where binJSON data starts.
 * @param array        Array of binary values that should be used when parsing.
 * @param arrayOffset  Where referenced binary values starts in the array.
 * @param bufferIndex  What is the starting index in binJSON of the binary value.
 * @returns            Parsed top-level value or BinJSON::Error if a parsing error occurs.
 */
std::shared_ptr<Value> parse(Buffer buffer, int32_t bufferOffset,
    const std::vector<Buffer>& array, int32_t arrayOffset, int32_t bufferIndex);

/** @brief Parse binJSON using the default buffer offsets.
 *
 * @param array Buffers containing the binJSON data and referenced binary values.
 * @returns Parsed top-level value, or an BinJSON::Error if the input is empty or parsing fails.
 */
static inline std::shared_ptr<Value> parse(const std::vector<Buffer>& array)
{
    if (array.size() == 0) {
        return std::make_shared<Error>("Empty input");
    }
    return parse(array[0], 0, array, 1, 1);
}

};

/** @brief Create a binJSON number from an unsigned integer literal. */
inline std::shared_ptr<BinJSON::Number> operator"" _binjson(unsigned long long value)
{
    return std::make_shared<BinJSON::Number>((double)value);
}

/** @brief Create a binJSON number from a floating-point literal. */
inline std::shared_ptr<BinJSON::Number> operator"" _binjson(long double value)
{
    return std::make_shared<BinJSON::Number>((double)value);
}

/** @brief Create a binJSON string from a string literal. */
inline std::shared_ptr<BinJSON::String> operator"" _binjson(const char* str, size_t length)
{
    return std::make_shared<BinJSON::String>(str, length);
}

#endif // BINJSON_HPP
