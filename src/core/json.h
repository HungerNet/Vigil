#pragma once
// Vigil — minimal JSON value, parser and writer (UTF-8, no external dependencies).

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vigil::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(double n) : type_(Type::Number), num_(n) {}
    Value(int n) : type_(Type::Number), num_(n) {}
    Value(std::int64_t n) : type_(Type::Number), num_(static_cast<double>(n)) {}
    Value(const char* s) : type_(Type::String), str_(s ? s : "") {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}

    static Value array() { Value v; v.type_ = Type::Array; return v; }
    static Value object() { Value v; v.type_ = Type::Object; return v; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    double asNumber(double fallback = 0.0) const { return type_ == Type::Number ? num_ : fallback; }
    const std::string& asString() const { static const std::string e; return type_ == Type::String ? str_ : e; }

    // Arrays
    std::size_t size() const { return type_ == Type::Array ? arr_.size() : (type_ == Type::Object ? obj_.size() : 0); }
    const Value& at(std::size_t i) const;
    void push(Value v);
    const std::vector<Value>& items() const { return arr_; }

    // Objects (insertion ordered)
    const Value* find(const std::string& key) const;
    Value& operator[](const std::string& key);  // creates; converts Null to Object
    const Value& get(const std::string& key) const;  // Null if missing
    const std::vector<std::pair<std::string, Value>>& members() const { return obj_; }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Value> arr_;
    std::vector<std::pair<std::string, Value>> obj_;
};

// Returns false and leaves `out` Null on malformed input. `error` receives a short message.
bool parse(const std::string& text, Value& out, std::string* error = nullptr);

// Serializes; `indent` < 0 produces compact output.
std::string write(const Value& v, int indent = 2);

std::string escape(const std::string& s);

}  // namespace vigil::json
