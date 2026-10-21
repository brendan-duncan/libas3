// Object, Class, Function, Boolean, Number, int, uint, Math, the Error
// classes and the top-level functions.

#include "builtins.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>

namespace as3 {

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInfinity = std::numeric_limits<double>::infinity();

std::shared_ptr<ClassInfo> classOfValue(const Val& value) {
    return value.isClass() ? value->cls : nullptr;
}

// Digits of a number in a radix, as Number.toString(radix) gives them.
std::string numberInRadix(double value, int radix) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    if (radix == 10) return numberToString(value);
    const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    const bool negative = value < 0;
    double magnitude = std::fabs(value);
    double whole = std::floor(magnitude);
    double fraction = magnitude - whole;
    std::string integer;
    if (whole == 0) {
        integer = "0";
    }
    while (whole > 0) {
        const double next = std::floor(whole / radix);
        const int digit = static_cast<int>(whole - next * radix);
        integer += digits[digit];
        whole = next;
    }
    std::reverse(integer.begin(), integer.end());
    std::string out = (negative ? "-" : "") + integer;
    if (fraction > 0) {
        out += ".";
        for (int i = 0; i < 52 && fraction > 0; ++i) {
            fraction *= radix;
            const int digit = static_cast<int>(std::floor(fraction));
            out += digits[digit];
            fraction -= digit;
        }
    }
    return out;
}

// The exact decimal digits of a non-negative number: integer part, and
// at least the fraction digits asked for.
void exactDigits(double value, int fractionDigits, std::string& integer, std::string& fraction) {
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "%.*f", std::min(fractionDigits + 30, 340), value);
    std::string text = buffer;
    const size_t point = text.find('.');
    integer = point == std::string::npos ? text : text.substr(0, point);
    fraction = point == std::string::npos ? std::string() : text.substr(point + 1);
}

// Rounds a digit string (integer.fraction) half up at a number of fraction
// digits.
std::string roundDecimal(std::string integer, std::string fraction, int digits) {
    while (static_cast<int>(fraction.size()) < digits + 1) fraction += '0';
    const bool up = fraction[static_cast<size_t>(digits)] >= '5';
    fraction = fraction.substr(0, static_cast<size_t>(digits));
    std::string all = integer + fraction;
    if (up) {
        int i = static_cast<int>(all.size()) - 1;
        for (; i >= 0; --i) {
            if (all[static_cast<size_t>(i)] == '9') {
                all[static_cast<size_t>(i)] = '0';
            } else {
                all[static_cast<size_t>(i)] += 1;
                break;
            }
        }
        if (i < 0) all.insert(all.begin(), '1');
    }
    const size_t integerSize = all.size() - static_cast<size_t>(digits);
    std::string out = all.substr(0, integerSize);
    if (digits > 0) out += "." + all.substr(integerSize);
    return out;
}

std::string toFixed(double value, int digits) {
    if (std::isnan(value)) return "NaN";
    if (std::fabs(value) >= 1e21) return numberToString(value);
    const bool negative = value < 0;
    std::string integer;
    std::string fraction;
    exactDigits(std::fabs(value), digits, integer, fraction);
    std::string out = roundDecimal(integer, fraction, digits);
    // -0.00 is 0.00.
    bool zero = true;
    for (char c : out) {
        if (c >= '1' && c <= '9') zero = false;
    }
    return (negative && !zero ? "-" : "") + out;
}

std::string toExponential(double value, int digits) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*e", digits, value);
    std::string text = buffer;
    // e+05 -> e+5
    const size_t e = text.find('e');
    std::string mantissa = text.substr(0, e);
    const int exponent = std::atoi(text.c_str() + e + 1);
    return mantissa + "e" + (exponent < 0 ? "-" : "+") + std::to_string(std::abs(exponent));
}

std::string toPrecision(double value, int precision) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    if (value == 0) {
        return precision > 1 ? "0." + std::string(static_cast<size_t>(precision - 1), '0') : "0";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*e", precision - 1, value);
    const int exponent = std::atoi(std::strchr(buffer, 'e') + 1);
    if (exponent < -6 || exponent >= precision) {
        return toExponential(value, precision - 1);
    }
    return toFixed(value, precision - 1 - exponent);
}

int parseRadix(const Val& value) {
    if (value.isUndefined()) return 10;
    const int radix = value.toInt32();
    return radix;
}

double parseIntText(const std::string& text, int radix) {
    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
        negative = text[i] == '-';
        ++i;
    }
    if (radix == 0) {
        if (i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            radix = 16;
            i += 2;
        } else if (i + 1 < text.size() && text[i] == '0') {
            radix = 8;
        } else {
            radix = 10;
        }
    } else if (radix == 16 && i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
        i += 2;
    }
    if (radix < 2 || radix > 36) return kNaN;
    double value = 0;
    bool any = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else break;
        if (digit >= radix) break;
        value = value * radix + digit;
        any = true;
    }
    if (!any) return kNaN;
    return negative ? -value : value;
}

double parseFloatText(const std::string& text) {
    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    const std::string rest = text.substr(i);
    if (rest.compare(0, 8, "Infinity") == 0 || rest.compare(0, 9, "+Infinity") == 0) return kInfinity;
    if (rest.compare(0, 9, "-Infinity") == 0) return -kInfinity;
    // The longest prefix that is a decimal number.
    size_t end = 0;
    if (end < rest.size() && (rest[end] == '+' || rest[end] == '-')) ++end;
    bool digits = false;
    while (end < rest.size() && std::isdigit(static_cast<unsigned char>(rest[end]))) {
        ++end;
        digits = true;
    }
    if (end < rest.size() && rest[end] == '.') {
        ++end;
        while (end < rest.size() && std::isdigit(static_cast<unsigned char>(rest[end]))) {
            ++end;
            digits = true;
        }
    }
    if (!digits) return kNaN;
    if (end < rest.size() && (rest[end] == 'e' || rest[end] == 'E')) {
        size_t e = end + 1;
        if (e < rest.size() && (rest[e] == '+' || rest[e] == '-')) ++e;
        if (e < rest.size() && std::isdigit(static_cast<unsigned char>(rest[e]))) {
            while (e < rest.size() && std::isdigit(static_cast<unsigned char>(rest[e]))) ++e;
            end = e;
        }
    }
    return std::strtod(rest.substr(0, end).c_str(), nullptr);
}

// escape and unescape, on UTF-16 units.
std::string escapeText(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    const std::u16string units = toUtf16(text);
    std::string out;
    for (char16_t unit : units) {
        if ((unit >= 'A' && unit <= 'Z') || (unit >= 'a' && unit <= 'z') || (unit >= '0' && unit <= '9') ||
            unit == '@' || unit == '-' || unit == '_' || unit == '.' || unit == '*' || unit == '+' || unit == '/') {
            out += static_cast<char>(unit);
        } else if (unit < 256) {
            out += '%';
            out += hex[unit >> 4];
            out += hex[unit & 15];
        } else {
            out += "%u";
            out += hex[(unit >> 12) & 15];
            out += hex[(unit >> 8) & 15];
            out += hex[(unit >> 4) & 15];
            out += hex[unit & 15];
        }
    }
    return out;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string unescapeText(const std::string& text) {
    std::u16string out;
    const std::u16string units = toUtf16(text);
    for (size_t i = 0; i < units.size(); ++i) {
        if (units[i] == '%') {
            if (i + 5 < units.size() + 0 && units[i + 1] == 'u') {
                int value = 0;
                bool ok = true;
                for (size_t k = 2; k < 6; ++k) {
                    const int d = units[i + k] < 128 ? hexDigit(static_cast<char>(units[i + k])) : -1;
                    if (d < 0) ok = false;
                    value = value * 16 + d;
                }
                if (ok) {
                    out += static_cast<char16_t>(value);
                    i += 5;
                    continue;
                }
            }
            if (i + 2 < units.size()) {
                const int a = units[i + 1] < 128 ? hexDigit(static_cast<char>(units[i + 1])) : -1;
                const int b = units[i + 2] < 128 ? hexDigit(static_cast<char>(units[i + 2])) : -1;
                if (a >= 0 && b >= 0) {
                    out += static_cast<char16_t>(a * 16 + b);
                    i += 2;
                    continue;
                }
            }
        }
        out += units[i];
    }
    return fromUtf16(out);
}

// encodeURI and encodeURIComponent: UTF-8 bytes, but those kept.
std::string encodeUri(const std::string& text, const std::string& keep) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || keep.find(static_cast<char>(c)) != std::string::npos) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string decodeUri(const std::string& text, const std::string& reserved) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int a = hexDigit(text[i + 1]);
            const int b = hexDigit(text[i + 2]);
            if (a >= 0 && b >= 0) {
                const char c = static_cast<char>(a * 16 + b);
                if (reserved.find(c) != std::string::npos && static_cast<unsigned char>(c) < 128) {
                    out += text.substr(i, 3);
                } else {
                    out += c;
                }
                i += 2;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

std::string traceText(VM& vm, Args args) {
    std::string line;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) line += " ";
        line += vm.toString(args[i]);
    }
    return line;
}

std::mt19937& randomEngine() {
    static std::mt19937 engine(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return engine;
}

} // namespace

namespace {

// A class's prototype as scripts see it: what it has is the class's
// prototype table (what its instances find after their own properties),
// and its constructor is the class.
struct PrototypeHost : HostObject {
    VM& vm;
    ClassInfo* cls;
    PrototypeHost(VM& machine, ClassInfo* of) : vm(machine), cls(of) {}

    ValuePtr get(const std::string& name) override {
        const auto found = cls->prototype.find(name);
        if (found != cls->prototype.end()) return ValuePtr(found->second);
        if (name == "constructor") return ValuePtr(vm.classValue(cls));
        return nullptr;
    }
    bool set(const std::string& name, ValuePtr value) override {
        cls->prototype[name] = Val(value);
        return true;
    }
    bool deleteIndex(const Val& key) override {
        if (!key.isString()) return false;
        cls->prototype.erase(key.str());
        return true;
    }
};

Val prototypeOf(VM& vm, const Val& cls) {
    if (!cls.isObject() || !cls->cls) return vm.newObject();
    ClassInfo* info = cls->cls.get();
    if (!info->prototypeObject.isObject()) {
        Val made = vm.newObject();
        made->host = std::make_shared<PrototypeHost>(vm, info);
        info->prototypeObject = made;
    }
    return info->prototypeObject;
}

} // namespace

void defineCoreBuiltins(VM& vm) {
    VM::Builtins& core = vm.builtins();

    // Object first: everything else extends it.
    Val object =
        vm.defineNativeClass("Object")
            .dynamic()
            .prototypeMethod("hasOwnProperty",
                             [](VM& vm, const Val& self, Args args) {
                                 if (!self.isObject()) return Val::boolean(false);
                                 const std::string name = args.string(0);
                                 Value* value = self.get();
                                 if (value->properties.count(name)) return Val::boolean(true);
                                 if (value->type == ValueType::Class) {
                                     return Val::boolean(value->cls && value->cls->findStatic(name));
                                 }
                                 if (value->type == ValueType::Array) {
                                     const double index = stringToNumber(name);
                                     if (index >= 0 && index < static_cast<double>(value->elements.size()) &&
                                         index == std::floor(index)) {
                                         return Val::boolean(true);
                                     }
                                 }
                                 const ClassInfo* cls = vm.classOf(self);
                                 if (cls) {
                                     const Trait* trait = cls->findTrait(name);
                                     if (trait && trait->kind != TraitKind::Method) return Val::boolean(true);
                                 }
                                 // (A proxy is not asked.)
                                 if (value->host && !value->host->answersEveryName() &&
                                     (value->host->get(name) || value->host->hasIndex(args[0]))) {
                                     return Val::boolean(true);
                                 }
                                 return Val::boolean(false);
                             },
                             1)
            .prototypeMethod("propertyIsEnumerable",
                             [](VM&, const Val& self, Args args) {
                                 if (!self.isObject()) return Val::boolean(false);
                                 const std::string name = args.string(0);
                                 if (self->properties.count(name)) return Val::boolean(true);
                                 if (self->type == ValueType::Array) {
                                     const double index = stringToNumber(name);
                                     return Val::boolean(index >= 0 &&
                                                         index < static_cast<double>(self->elements.size()));
                                 }
                                 return Val::boolean(false);
                             },
                             1)
            .prototypeMethod("setPropertyIsEnumerable", [](VM&, const Val&, Args) { return Val(); }, 2)
            .prototypeMethod("isPrototypeOf", [](VM&, const Val&, Args) { return Val::boolean(false); }, 1)
            .prototypeMethod("toString",
                             [](VM& vm, const Val& self, Args) {
                                 if (self.isClass()) return Val::string("[class " + self->cls->shortName + "]");
                                 const ClassInfo* cls = vm.classOf(self);
                                 return Val::string("[object " + (cls ? cls->shortName : std::string("Object")) +
                                                    "]");
                             })
            .prototypeMethod("toLocaleString",
                             [](VM& vm, const Val& self, Args) { return Val::string(vm.toString(self)); })
            .prototypeMethod("valueOf", [](VM&, const Val& self, Args) { return self; })
            .callHandler([](VM& vm, const Val&, Args args) {
                if (args.empty() || args[0].isNullish()) return vm.newObject();
                return args[0];
            })
            .define();
    core.object = classOfValue(object);

    core.klass = classOfValue(vm.defineNativeClass("Class")
                                  .getter("prototype", [](VM& vm, const Val& self, Args) { return prototypeOf(vm, self); })
                                  .define());

    Val function =
        vm.defineNativeClass("Function")
            .dynamic()
            .method("call",
                    [](VM& vm, const Val& self, Args args) {
                        Args rest(args.size() > 1 ? args.begin() + 1 : nullptr, args.size() > 1 ? args.size() - 1 : 0);
                        return vm.callFunction(self, args[0], rest);
                    },
                    1)
            .method("apply",
                    [](VM& vm, const Val& self, Args args) {
                        std::vector<Val> list;
                        if (args.size() > 1 && args[1].isArray()) {
                            list = args[1]->elements;
                        } else if (args.size() > 1 && !args[1].isNullish()) {
                            vm.throwError("TypeError", "Error #1116: second argument to Function.prototype.apply must be an array.",
                                          1116);
                        }
                        return vm.callFunction(self, args[0], Args(list.data(), list.size()));
                    },
                    2)
            .getter("length",
                    [](VM&, const Val& self, Args) {
                        if (self.isObject() && self->function) {
                            return Val::number(self->function->length);
                        }
                        return Val::number(0);
                    })
            .prototypeMethod("toString", [](VM&, const Val&, Args) { return Val::string("function Function() {}"); })
            .callHandler([](VM& vm, const Val&, Args) -> Val {
                vm.throwError("EvalError", "Error #1066: The form function('function body') is not supported.", 1066);
            })
            .define();
    core.function = classOfValue(function);

    // Boolean, Number, int, uint: what their values have, and conversions.
    Val boolean = vm.defineNativeClass("Boolean")
                      .final()
                      .constructor([](VM&, const Val&, Args args) { return Val::boolean(args[0].toBoolean()); })
                      .callHandler([](VM&, const Val&, Args args) { return Val::boolean(args[0].toBoolean()); })
                      .prototypeMethod("toString",
                                       [](VM&, const Val& self, Args) { return Val::string(self.toString()); })
                      .prototypeMethod("valueOf", [](VM&, const Val& self, Args) { return self; })
                      .define();
    core.boolean = classOfValue(boolean);

    const auto numberMethods = [](NativeClassBuilder& builder) -> NativeClassBuilder& {
        return builder
            .method("toString",
                    [](VM& vm, const Val& self, Args args) {
                        const int radix = args.empty() || args[0].isUndefined() ? 10 : args[0].toInt32();
                        if (radix < 2 || radix > 36) {
                            vm.throwError("RangeError",
                                          "Error #1003: The radix argument must be between 2 and 36; got " +
                                              std::to_string(radix) + ".",
                                          1003);
                        }
                        return Val::string(numberInRadix(self.toNumber(), radix));
                    },
                    1)
            .method("toLocaleString",
                    [](VM&, const Val& self, Args) { return Val::string(numberToString(self.toNumber())); })
            .method("valueOf", [](VM&, const Val& self, Args) { return Val::number(self.toNumber()); })
            .method("toFixed",
                    [](VM& vm, const Val& self, Args args) {
                        const int digits = args.integer(0, 0);
                        if (digits < 0 || digits > 20) {
                            vm.throwError("RangeError",
                                          "Error #1002: Number.toFixed has a range of 0 to 20. Specified value is not "
                                          "within expected range.",
                                          1002);
                        }
                        return Val::string(toFixed(self.toNumber(), digits));
                    },
                    1)
            .method("toPrecision",
                    [](VM& vm, const Val& self, Args args) {
                        if (args.empty() || args[0].isUndefined()) return Val::string(numberToString(self.toNumber()));
                        const int precision = args.integer(0, 1);
                        if (precision < 1 || precision > 21) {
                            vm.throwError("RangeError",
                                          "Error #1002: Number.toPrecision has a range of 1 to 21. Specified value is "
                                          "not within expected range.",
                                          1002);
                        }
                        return Val::string(toPrecision(self.toNumber(), precision));
                    },
                    1)
            .method("toExponential",
                    [](VM& vm, const Val& self, Args args) {
                        const int digits = args.integer(0, 0);
                        if (digits < 0 || digits > 20) {
                            vm.throwError("RangeError",
                                          "Error #1002: Number.toExponential has a range of 0 to 20. Specified value "
                                          "is not within expected range.",
                                          1002);
                        }
                        return Val::string(toExponential(self.toNumber(), digits));
                    },
                    1);
    };
    {
        NativeClassBuilder builder = vm.defineNativeClass("Number");
        numberMethods(builder)
            .final()
            .constant("MAX_VALUE", Val::number(std::numeric_limits<double>::max()))
            .constant("MIN_VALUE", Val::number(std::numeric_limits<double>::denorm_min()))
            .constant("NaN", Val::number(kNaN))
            .constant("NEGATIVE_INFINITY", Val::number(-kInfinity))
            .constant("POSITIVE_INFINITY", Val::number(kInfinity))
            .constructor([](VM& vm, const Val&, Args args) {
                return Val::number(args.empty() ? 0 : vm.toNumber(args[0]));
            })
            .callHandler([](VM& vm, const Val&, Args args) {
                return Val::number(args.empty() ? 0 : vm.toNumber(args[0]));
            });
        core.number = classOfValue(builder.define());
    }
    {
        NativeClassBuilder builder = vm.defineNativeClass("int");
        numberMethods(builder)
            .final()
            .constant("MAX_VALUE", Val::number(2147483647.0))
            .constant("MIN_VALUE", Val::number(-2147483648.0))
            .constructor([](VM& vm, const Val&, Args args) {
                return Val::number(args.empty() ? 0 : toInt32(vm.toNumber(args[0])));
            })
            .callHandler([](VM& vm, const Val&, Args args) {
                return Val::number(args.empty() ? 0 : toInt32(vm.toNumber(args[0])));
            });
        core.intClass = classOfValue(builder.define());
    }
    {
        NativeClassBuilder builder = vm.defineNativeClass("uint");
        numberMethods(builder)
            .final()
            .constant("MAX_VALUE", Val::number(4294967295.0))
            .constant("MIN_VALUE", Val::number(0))
            .constructor([](VM& vm, const Val&, Args args) {
                return Val::number(args.empty() ? 0 : toUint32(vm.toNumber(args[0])));
            })
            .callHandler([](VM& vm, const Val&, Args args) {
                return Val::number(args.empty() ? 0 : toUint32(vm.toNumber(args[0])));
            });
        core.uintClass = classOfValue(builder.define());
    }

    // Math
    {
        NativeClassBuilder math = vm.defineNativeClass("Math");
        math.final()
            .constant("E", Val::number(2.718281828459045))
            .constant("LN10", Val::number(2.302585092994046))
            .constant("LN2", Val::number(0.6931471805599453))
            .constant("LOG10E", Val::number(0.4342944819032518))
            .constant("LOG2E", Val::number(1.442695040888963387))
            .constant("PI", Val::number(3.141592653589793))
            .constant("SQRT1_2", Val::number(0.7071067811865476))
            .constant("SQRT2", Val::number(1.4142135623730951));
        const auto unary = [&math](const char* name, double (*f)(double)) {
            math.staticMethod(name, [f](VM& vm, const Val&, Args args) {
                return Val::number(f(args.empty() ? kNaN : vm.toNumber(args[0])));
            }, 1);
        };
        unary("abs", [](double x) { return std::fabs(x); });
        unary("acos", [](double x) { return std::acos(x); });
        unary("asin", [](double x) { return std::asin(x); });
        unary("atan", [](double x) { return std::atan(x); });
        unary("ceil", [](double x) { return std::ceil(x); });
        unary("cos", [](double x) { return std::cos(x); });
        unary("exp", [](double x) { return std::exp(x); });
        unary("floor", [](double x) { return std::floor(x); });
        unary("log", [](double x) { return std::log(x); });
        unary("round", [](double x) {
            if (std::isnan(x) || std::isinf(x)) return x;
            return std::floor(x + 0.5);
        });
        unary("sin", [](double x) { return std::sin(x); });
        unary("sqrt", [](double x) { return std::sqrt(x); });
        unary("tan", [](double x) { return std::tan(x); });
        math.staticMethod("atan2", [](VM& vm, const Val&, Args args) {
            return Val::number(std::atan2(vm.toNumber(args[0]), vm.toNumber(args[1])));
        }, 2);
        math.staticMethod("pow", [](VM& vm, const Val&, Args args) {
            const double x = vm.toNumber(args[0]);
            const double y = vm.toNumber(args[1]);
            if (std::isnan(y)) return Val::number(kNaN);
            if (y == 0) return Val::number(1);
            if ((x == 1 || x == -1) && std::isinf(y)) return Val::number(kNaN);
            return Val::number(std::pow(x, y));
        }, 2);
        math.staticMethod("max", [](VM& vm, const Val&, Args args) {
            double best = -kInfinity;
            for (const Val& arg : args) {
                const double x = vm.toNumber(arg);
                if (std::isnan(x)) return Val::number(kNaN);
                if (x > best || (x == 0 && best == 0 && !std::signbit(x))) best = x;
            }
            return Val::number(best);
        }, 2);
        math.staticMethod("min", [](VM& vm, const Val&, Args args) {
            double best = kInfinity;
            for (const Val& arg : args) {
                const double x = vm.toNumber(arg);
                if (std::isnan(x)) return Val::number(kNaN);
                if (x < best || (x == 0 && best == 0 && std::signbit(x))) best = x;
            }
            return Val::number(best);
        }, 2);
        math.staticMethod("random", [](VM&, const Val&, Args) {
            std::uniform_real_distribution<double> unit(0.0, 1.0);
            return Val::number(unit(randomEngine()));
        });
        core.math = classOfValue(math.define());
    }

    // Errors: message, name and errorID, and where they were made.
    Val error =
        vm.defineNativeClass("Error")
            .dynamic()
            .field("message", Val::string(""))
            .field("name", Val::string("Error"))
            .field("$errorID", Val::number(0))
            .field("$stack", Val::string(""))
            .constructor([](VM& vm, const Val& self, Args args) {
                const Val message = args.empty() || args[0].isUndefined() ? Val::string("") : Val::string(vm.toString(args[0]));
                vm.setProperty(self, "message", message);
                vm.setProperty(self, "$errorID", Val::number(args.integer(1, 0)));
                // Its name: the built-in error class it is.
                const ClassInfo* cls = vm.classOf(self);
                while (cls && !cls->isNative) cls = cls->base.get();
                if (cls) vm.setProperty(self, "name", Val::string(cls->shortName));
                vm.setProperty(self, "$stack", Val::string(vm.stackTrace()));
                return Val();
            })
            .getter("errorID", [](VM& vm, const Val& self, Args) { return vm.getProperty(self, "$errorID"); })
            .method("getStackTrace",
                    [](VM& vm, const Val& self, Args) {
                        // As the player gives it: no line break after the last frame.
                        std::string stack = vm.getProperty(self, "$stack").toString();
                        while (!stack.empty() && stack.back() == '\n') stack.pop_back();
                        return Val::string(stack.empty() ? vm.toString(self) : vm.toString(self) + "\n" + stack);
                    })
            .prototypeMethod("toString",
                             [](VM& vm, const Val& self, Args) {
                                 const std::string name = vm.toString(vm.getProperty(self, "name"));
                                 const std::string message = vm.toString(vm.getProperty(self, "message"));
                                 return Val::string(message.empty() ? name : name + ": " + message);
                             })
            .define();
    core.error = classOfValue(error);
    for (const char* name : {"TypeError", "ReferenceError", "RangeError", "ArgumentError", "SyntaxError",
                             "EvalError", "URIError", "SecurityError", "VerifyError", "DefinitionError",
                             "UninitializedError"}) {
        vm.defineNativeClass(name).extends("Error").dynamic().define();
    }
    for (const char* name : {"flash.errors.IOError", "flash.errors.IllegalOperationError",
                             "flash.errors.InvalidSWFError", "flash.errors.MemoryError",
                             "flash.errors.ScriptTimeoutError", "flash.errors.StackOverflowError"}) {
        vm.defineNativeClass(name).extends("Error").dynamic().define();
    }
    vm.defineNativeClass("flash.errors.EOFError").extends("flash.errors.IOError").dynamic().define();

    // Top-level functions and values.
    vm.defineValue("undefined", Val());
    vm.defineValue("NaN", Val::number(kNaN));
    vm.defineValue("Infinity", Val::number(kInfinity));
    vm.defineFunction("trace", [](VM& vm, const Val&, Args args) {
        std::cout << traceText(vm, args) << std::endl;
        return Val();
    }, 1);
    vm.defineFunction("print", [](VM& vm, const Val&, Args args) {
        std::cout << traceText(vm, args) << std::endl;
        return Val();
    }, 1);
    vm.defineFunction("isNaN", [](VM& vm, const Val&, Args args) {
        return Val::boolean(std::isnan(args.empty() ? kNaN : vm.toNumber(args[0])));
    }, 1);
    vm.defineFunction("isFinite", [](VM& vm, const Val&, Args args) {
        const double x = args.empty() ? kNaN : vm.toNumber(args[0]);
        return Val::boolean(!std::isnan(x) && !std::isinf(x));
    }, 1);
    vm.defineFunction("parseInt", [](VM& vm, const Val&, Args args) {
        const int radix = args.size() > 1 && !args[1].isUndefined() ? args[1].toInt32() : 0;
        return Val::number(parseIntText(vm.toString(args[0]), radix));
    }, 2);
    vm.defineFunction("parseFloat", [](VM& vm, const Val&, Args args) {
        return Val::number(parseFloatText(vm.toString(args[0])));
    }, 1);
    vm.defineFunction("escape", [](VM& vm, const Val&, Args args) {
        return Val::string(escapeText(args.empty() ? "undefined" : vm.toString(args[0])));
    }, 1);
    vm.defineFunction("unescape", [](VM& vm, const Val&, Args args) {
        return Val::string(unescapeText(args.empty() ? "undefined" : vm.toString(args[0])));
    }, 1);
    vm.defineFunction("encodeURI", [](VM& vm, const Val&, Args args) {
        return Val::string(encodeUri(vm.toString(args[0]), ";/?:@&=+$,-_.!~*'()#"));
    }, 1);
    vm.defineFunction("encodeURIComponent", [](VM& vm, const Val&, Args args) {
        return Val::string(encodeUri(vm.toString(args[0]), "-_.!~*'()"));
    }, 1);
    vm.defineFunction("decodeURI", [](VM& vm, const Val&, Args args) {
        return Val::string(decodeUri(vm.toString(args[0]), ";/?:@&=+$,#"));
    }, 1);
    vm.defineFunction("decodeURIComponent", [](VM& vm, const Val&, Args args) {
        return Val::string(decodeUri(vm.toString(args[0]), ""));
    }, 1);
    vm.defineFunction("isXMLName", [](VM& vm, const Val&, Args args) {
        const std::string name = vm.toString(args[0]);
        bool ok = !name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_');
        for (char c : name) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') ok = false;
        }
        return Val::boolean(ok);
    }, 1);
}

void VM::setupBuiltins() {
    defineCoreBuiltins(*this);
    defineArrayBuiltins(*this);
    defineStringBuiltins(*this);
    defineDateBuiltins(*this);
    defineJsonBuiltins(*this);
    defineNamespaceBuiltins(*this);
    defineUtilsBuiltins(*this);
    defineXmlBuiltins(*this);
}

} // namespace as3
