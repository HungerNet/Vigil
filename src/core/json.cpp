#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace vigil::json {

const Value& Value::at(std::size_t i) const {
    static const Value null;
    return (type_ == Type::Array && i < arr_.size()) ? arr_[i] : null;
}

void Value::push(Value v) {
    if (type_ != Type::Array) { *this = array(); }
    arr_.push_back(std::move(v));
}

const Value* Value::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& kv : obj_) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

Value& Value::operator[](const std::string& key) {
    if (type_ != Type::Object) { *this = object(); }
    for (auto& kv : obj_) {
        if (kv.first == key) return kv.second;
    }
    obj_.emplace_back(key, Value());
    return obj_.back().second;
}

const Value& Value::get(const std::string& key) const {
    static const Value null;
    const Value* v = find(key);
    return v ? *v : null;
}

namespace {

struct Parser {
    const std::string& s;
    std::size_t i = 0;
    std::string err;
    int depth = 0;

    explicit Parser(const std::string& text) : s(text) {}

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool fail(const char* m) { if (err.empty()) err = m; return false; }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(unsigned& out) {
        if (i + 4 > s.size()) return fail("truncated \\u escape");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("bad hex digit");
        }
        return true;
    }

    bool str(std::string& out) {
        if (s[i] != '"') return fail("expected string");
        ++i;
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return fail("bad escape");
            const char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    bool value(Value& out) {
        if (++depth > 64) return fail("nesting too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        const char c = s[i];
        bool ok = false;
        if (c == '{') {
            ++i;
            out = Value::object();
            ws();
            if (i < s.size() && s[i] == '}') { ++i; ok = true; }
            else {
                while (true) {
                    ws();
                    std::string key;
                    if (i >= s.size() || !str(key)) return fail("expected key");
                    ws();
                    if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                    ++i;
                    Value v;
                    if (!value(v)) return false;
                    out[key] = std::move(v);
                    ws();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == '}') { ++i; ok = true; break; }
                    return fail("expected ',' or '}'");
                }
            }
        } else if (c == '[') {
            ++i;
            out = Value::array();
            ws();
            if (i < s.size() && s[i] == ']') { ++i; ok = true; }
            else {
                while (true) {
                    Value v;
                    if (!value(v)) return false;
                    out.push(std::move(v));
                    ws();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == ']') { ++i; ok = true; break; }
                    return fail("expected ',' or ']'");
                }
            }
        } else if (c == '"') {
            std::string v;
            ok = str(v);
            if (ok) out = Value(std::move(v));
        } else if (s.compare(i, 4, "true") == 0) { i += 4; out = Value(true); ok = true; }
        else if (s.compare(i, 5, "false") == 0) { i += 5; out = Value(false); ok = true; }
        else if (s.compare(i, 4, "null") == 0) { i += 4; out = Value(); ok = true; }
        else {
            const char* begin = s.c_str() + i;
            char* end = nullptr;
            const double d = std::strtod(begin, &end);
            if (end == begin) return fail("unexpected character");
            i += static_cast<std::size_t>(end - begin);
            out = Value(d);
            ok = true;
        }
        --depth;
        return ok;
    }
};

void writeImpl(const Value& v, std::string& out, int indent, int level) {
    auto nl = [&](int lvl) {
        if (indent < 0) return;
        out += '\n';
        out.append(static_cast<std::size_t>(indent * lvl), ' ');
    };
    switch (v.type()) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Value::Type::Number: {
            const double d = v.asNumber();
            if (!std::isfinite(d)) { out += "0"; break; }
            char buf[64];
            // Shortest form that reads back as the same double: traces and saved profiles replay
            // bit-for-bit (docs/VALIDATION.md), and settings stay readable ("0.4", not "0.40000000000000002").
            if (std::floor(d) == d && std::fabs(d) < 1e15) std::snprintf(buf, sizeof(buf), "%.0f", d);
            else {
                std::snprintf(buf, sizeof(buf), "%.15g", d);
                if (std::strtod(buf, nullptr) != d) std::snprintf(buf, sizeof(buf), "%.17g", d);
            }
            out += buf;
            break;
        }
        case Value::Type::String: out += '"'; out += escape(v.asString()); out += '"'; break;
        case Value::Type::Array: {
            out += '[';
            const auto& items = v.items();
            for (std::size_t k = 0; k < items.size(); ++k) {
                nl(level + 1);
                writeImpl(items[k], out, indent, level + 1);
                if (k + 1 < items.size()) out += ',';
            }
            if (!items.empty()) nl(level);
            out += ']';
            break;
        }
        case Value::Type::Object: {
            out += '{';
            const auto& mem = v.members();
            for (std::size_t k = 0; k < mem.size(); ++k) {
                nl(level + 1);
                out += '"';
                out += escape(mem[k].first);
                out += indent < 0 ? "\":" : "\": ";
                writeImpl(mem[k].second, out, indent, level + 1);
                if (k + 1 < mem.size()) out += ',';
            }
            if (!mem.empty()) nl(level);
            out += '}';
            break;
        }
    }
}

}  // namespace

bool parse(const std::string& text, Value& out, std::string* error) {
    Parser p(text);
    Value v;
    if (!p.value(v)) {
        if (error) *error = p.err;
        out = Value();
        return false;
    }
    p.ws();
    if (p.i != text.size()) {
        if (error) *error = "trailing characters";
        out = Value();
        return false;
    }
    out = std::move(v);
    return true;
}

std::string write(const Value& v, int indent) {
    std::string out;
    writeImpl(v, out, indent, 0);
    return out;
}

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

}  // namespace vigil::json
