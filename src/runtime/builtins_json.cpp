// JSON.parse and JSON.stringify.

#include "builtins.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace as3 {

namespace {

class JsonReader {
public:
    JsonReader(VM& vm, const std::string& text) : _vm(vm), _text(text) {}

    Val read() {
        Val value = readValue();
        skipSpace();
        if (_at != _text.size()) fail();
        return value;
    }

private:
    VM& _vm;
    const std::string& _text;
    size_t _at = 0;

    [[noreturn]] void fail() { _vm.throwError("SyntaxError", "Error #1132: Invalid JSON parse input.", 1132); }

    void skipSpace() {
        while (_at < _text.size() &&
               (_text[_at] == ' ' || _text[_at] == '\t' || _text[_at] == '\n' || _text[_at] == '\r')) {
            ++_at;
        }
    }

    bool consume(const char* word) {
        const size_t length = std::char_traits<char>::length(word);
        if (_text.compare(_at, length, word) == 0) {
            _at += length;
            return true;
        }
        return false;
    }

    Val readValue() {
        skipSpace();
        if (_at >= _text.size()) fail();
        const char c = _text[_at];
        if (c == '{') return readObject();
        if (c == '[') return readArray();
        if (c == '"') return Val::string(readString());
        if (consume("true")) return Val::boolean(true);
        if (consume("false")) return Val::boolean(false);
        if (consume("null")) return Val::null();
        if (c == '-' || (c >= '0' && c <= '9')) return readNumber();
        fail();
    }

    Val readNumber() {
        const size_t start = _at;
        if (_text[_at] == '-') ++_at;
        if (_at >= _text.size() || !std::isdigit(static_cast<unsigned char>(_text[_at]))) fail();
        while (_at < _text.size() && std::isdigit(static_cast<unsigned char>(_text[_at]))) ++_at;
        if (_at < _text.size() && _text[_at] == '.') {
            ++_at;
            if (_at >= _text.size() || !std::isdigit(static_cast<unsigned char>(_text[_at]))) fail();
            while (_at < _text.size() && std::isdigit(static_cast<unsigned char>(_text[_at]))) ++_at;
        }
        if (_at < _text.size() && (_text[_at] == 'e' || _text[_at] == 'E')) {
            ++_at;
            if (_at < _text.size() && (_text[_at] == '+' || _text[_at] == '-')) ++_at;
            if (_at >= _text.size() || !std::isdigit(static_cast<unsigned char>(_text[_at]))) fail();
            while (_at < _text.size() && std::isdigit(static_cast<unsigned char>(_text[_at]))) ++_at;
        }
        return Val::number(std::strtod(_text.substr(start, _at - start).c_str(), nullptr));
    }

    std::string readString() {
        ++_at;  // "
        std::u16string units;
        std::string plain;
        bool simple = true;
        while (_at < _text.size() && _text[_at] != '"') {
            const char c = _text[_at];
            if (static_cast<unsigned char>(c) < 0x20) fail();
            if (c == '\\') {
                if (simple) {
                    units = toUtf16(plain);
                    simple = false;
                }
                ++_at;
                if (_at >= _text.size()) fail();
                const char e = _text[_at++];
                switch (e) {
                    case '"': units += u'"'; break;
                    case '\\': units += u'\\'; break;
                    case '/': units += u'/'; break;
                    case 'b': units += u'\b'; break;
                    case 'f': units += u'\f'; break;
                    case 'n': units += u'\n'; break;
                    case 'r': units += u'\r'; break;
                    case 't': units += u'\t'; break;
                    case 'u': {
                        if (_at + 4 > _text.size()) fail();
                        const std::string hex = _text.substr(_at, 4);
                        for (char h : hex) {
                            if (!std::isxdigit(static_cast<unsigned char>(h))) fail();
                        }
                        units += static_cast<char16_t>(std::strtol(hex.c_str(), nullptr, 16));
                        _at += 4;
                        break;
                    }
                    default:
                        fail();
                }
                continue;
            }
            if (simple) {
                plain += c;
                ++_at;
            } else {
                // A run of plain text, as UTF-16.
                size_t end = _at;
                while (end < _text.size() && _text[end] != '"' && _text[end] != '\\') ++end;
                units += toUtf16(_text.substr(_at, end - _at));
                _at = end;
            }
        }
        if (_at >= _text.size()) fail();
        ++_at;
        return simple ? plain : fromUtf16(units);
    }

    Val readArray() {
        ++_at;
        std::vector<Val> elements;
        skipSpace();
        if (_at < _text.size() && _text[_at] == ']') {
            ++_at;
            return _vm.newArray(std::move(elements));
        }
        for (;;) {
            elements.push_back(readValue());
            skipSpace();
            if (_at < _text.size() && _text[_at] == ',') {
                ++_at;
                continue;
            }
            if (_at < _text.size() && _text[_at] == ']') {
                ++_at;
                break;
            }
            fail();
        }
        return _vm.newArray(std::move(elements));
    }

    Val readObject() {
        ++_at;
        Val object = _vm.newObject();
        skipSpace();
        if (_at < _text.size() && _text[_at] == '}') {
            ++_at;
            return object;
        }
        for (;;) {
            skipSpace();
            if (_at >= _text.size() || _text[_at] != '"') fail();
            const std::string key = readString();
            skipSpace();
            if (_at >= _text.size() || _text[_at] != ':') fail();
            ++_at;
            object->properties[key] = readValue();
            skipSpace();
            if (_at < _text.size() && _text[_at] == ',') {
                ++_at;
                continue;
            }
            if (_at < _text.size() && _text[_at] == '}') {
                ++_at;
                break;
            }
            fail();
        }
        return object;
    }
};

Val revive(VM& vm, const Val& holder, const Val& key, const Val& reviver) {
    Val value = vm.getIndex(holder, key);
    if (value.isObject()) {
        if (value.isArray()) {
            for (size_t i = 0; i < value->elements.size(); ++i) {
                const Val index = Val::number(static_cast<double>(i));
                Val revived = revive(vm, value, index, reviver);
                value->elements[i] = revived;
            }
        } else {
            std::vector<std::string> keys;
            for (const auto& property : value->properties) keys.push_back(property.first);
            for (const auto& name : keys) {
                Val revived = revive(vm, value, Val::string(name), reviver);
                if (revived.isUndefined()) {
                    value->properties.erase(name);
                } else {
                    value->properties[name] = revived;
                }
            }
        }
    }
    return vm.callFunction(reviver, holder, {Val::string(key.toString()), value});
}

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                    out += escaped;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out + "\"";
}

class JsonWriter {
public:
    JsonWriter(VM& vm, const Val& replacer, std::string gap) : _vm(vm), _replacer(replacer), _gap(std::move(gap)) {
        if (_replacer.isArray()) {
            for (const Val& name : _replacer->elements) _allowed.push_back(_vm.toString(name));
            _useAllowed = true;
        }
    }

    // False when the value has no JSON (a function, undefined).
    bool write(const Val& holder, const std::string& key, Val value, const std::string& indent, std::string& out) {
        if (value.isObject()) {
            Val toJson = _vm.getProperty(value, "toJSON");
            if (toJson.isFunction()) value = _vm.callFunction(toJson, value, {Val::string(key)});
        }
        if (_replacer.isFunction()) {
            value = _vm.callFunction(_replacer, holder, {Val::string(key), value});
        }
        switch (value.tag) {
            case Val::Tag::Null:
                out += "null";
                return true;
            case Val::Tag::Boolean:
                out += value.flag ? "true" : "false";
                return true;
            case Val::Tag::Number:
                out += std::isfinite(value.num) ? numberToString(value.num) : "null";
                return true;
            case Val::Tag::String:
                out += quote(value.str());
                return true;
            case Val::Tag::Undefined:
                return false;
            default:
                break;
        }
        if (value->isFunction() || value->type == ValueType::Class) {
            return false;
        }
        if (_seen.count(value.get())) {
            _vm.throwError("TypeError", "Error #1129: Cyclic structure cannot be converted to JSON string.", 1129);
        }
        _seen.insert(value.get());
        const std::string inner = indent + _gap;
        if (value.isArray()) {
            out += "[";
            for (size_t i = 0; i < value->elements.size(); ++i) {
                if (i > 0) out += ",";
                if (!_gap.empty()) out += "\n" + inner;
                if (!write(value, std::to_string(i), value->elements[i], inner, out)) out += "null";
            }
            if (!_gap.empty() && !value->elements.empty()) out += "\n" + indent;
            out += "]";
        } else {
            out += "{";
            bool first = true;
            for (const std::string& name : keysOf(value)) {
                std::string part;
                if (!write(value, name, _vm.getProperty(value, name), inner, part)) continue;
                if (!first) out += ",";
                first = false;
                if (!_gap.empty()) out += "\n" + inner;
                out += quote(name) + (_gap.empty() ? ":" : ": ") + part;
            }
            if (!_gap.empty() && !first) out += "\n" + indent;
            out += "}";
        }
        _seen.erase(value.get());
        return true;
    }

private:
    VM& _vm;
    Val _replacer;
    std::string _gap;
    std::vector<std::string> _allowed;
    bool _useAllowed = false;
    std::set<const Value*> _seen;

    // A class instance's fields and getters, then its dynamic properties.
    std::vector<std::string> keysOf(const Val& value) {
        if (_useAllowed) return _allowed;
        std::vector<std::string> keys;
        const ClassInfo* cls = value->cls.get();
        if (cls && cls != _vm.builtins().object.get()) {
            for (const auto& trait : cls->traits) {
                if (trait.first.empty() || trait.first[0] == '$' || trait.first.find("::") != std::string::npos) continue;
                if (trait.second.kind == TraitKind::Slot ||
                    (trait.second.kind == TraitKind::Accessor && trait.second.getter.isObject())) {
                    keys.push_back(trait.first);
                }
            }
            std::sort(keys.begin(), keys.end());
        }
        for (const auto& property : value->properties) keys.push_back(property.first);
        return keys;
    }
};

} // namespace

void defineJsonBuiltins(VM& vm) {
    vm.defineNativeClass("JSON")
        .final()
        .staticMethod("parse",
                      [](VM& vm, const Val&, Args args) {
                          const std::string text = vm.toString(args[0]);
                          Val value = JsonReader(vm, text).read();
                          if (args[1].isFunction()) {
                              Val root = vm.newObject();
                              root->properties[""] = value;
                              return revive(vm, root, Val::string(""), args[1]);
                          }
                          return value;
                      },
                      2)
        .staticMethod("stringify",
                      [](VM& vm, const Val&, Args args) {
                          std::string gap;
                          if (args[2].isNumber()) {
                              gap.assign(static_cast<size_t>(std::max(0, std::min(10, args[2].toInt32()))), ' ');
                          } else if (args[2].isString()) {
                              gap = args[2].str().substr(0, 10);
                          }
                          JsonWriter writer(vm, args[1], gap);
                          Val root = vm.newObject();
                          root->properties[""] = args[0];
                          std::string out;
                          if (!writer.write(root, "", args[0], "", out)) return Val();
                          return Val::string(out);
                      },
                      3)
        .define();
}

} // namespace as3
