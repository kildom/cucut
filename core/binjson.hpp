#ifndef BINJSON_HPP
#define BINJSON_HPP

#include <stdint.h>
#include <string>
#include <limits>

#include "buffer.hpp"


namespace BinJSONType {
enum {
    NUL = 0,
    BUFFER = 1,
    NUMBER = 2,
    STRING = 3,
    ARRAY = 4,
    OBJECT = 5,
} T;
};


class BinJSONValue {

public:

    virtual BinJSONType:T getType() = 0;
    virtual bool isNull() { return false; }
    virtual Buffer buffer() { return Buffer(0); }
    virtual double number() { return std::numeric_limits<double>::quiet_NaN(); }
    virtual std::string& string() { static std::string empty; return empty; }
    virtual int length() { return 0; }
    virtual std::shared_ptr<BinJSONValue> get(int index) { return std::make_shared<BinJSONNull>(); }
    virtual std::shared_ptr<BinJSONValue> get(const std::string& name) { return std::make_shared<BinJSONNull>(); }
    virtual ~BinJSONValue() {}

    std::shared_ptr<BinJSONValue> operator[](int index) { return get(index); }
    std::shared_ptr<BinJSONValue> operator[](const std::string& name) { return get(name); }

};


class BinJSONNull: public BinJSONValue {

public:
    virtual BinJSONType:T getType() { return BinJSONType::NUL; }
    virtual bool isNull() { return true; }
};


class BinJSONBuffer: public BinJSONValue {

public:
    Buffer value;
    virtual BinJSONType:T getType() { return BinJSONType::BUFFER; }
    virtual Buffer buffer() { return value; }
};

class BinJSONNumber: public BinJSONValue {

public:
    double value;
    virtual BinJSONType:T getType() { return BinJSONType::NUMBER; }
    virtual double number() { return value; }
};


class BinJSONString: public BinJSONValue
{
public:
    std::string value;
    virtual BinJSONType:T getType() { return BinJSONType::STRING; }
    virtual std::string& string() { return value; }
};

class BinJSONContainer: public BinJSONValue
{

};

class BinJSONArray: public BinJSONContainer
{
public:
    std::vector<std::shared_ptr<BinJSONValue>> value;
    virtual std::shared_ptr<BinJSONArray> getArray() { return std::make_shared<BinJSONArray>(); }
    virtual std::shared_ptr<BinJSONObject> getObject() { return std::make_shared<BinJSONObject>(); }
}


#endif // BINJSON_HPP
