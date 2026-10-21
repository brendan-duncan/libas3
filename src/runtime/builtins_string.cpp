// String and RegExp. Strings are kept as UTF-8; their methods count in
// UTF-16 units, as ActionScript does. Regular expressions run on
// std::wregex with ECMAScript syntax, with ActionScript's s (dotall) and x
// (extended) flags and (?P<name>...) groups worked in.

#include "builtins.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <map>
#include <regex>

namespace as3 {

std::u16string toUtf16(const std::string& text) {
    std::u16string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t code;
        size_t extra;
        if (c < 0x80) {
            code = c;
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            code = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            code = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            code = c & 0x07;
            extra = 3;
        } else {
            out += static_cast<char16_t>(0xFFFD);
            ++i;
            continue;
        }
        bool ok = true;
        for (size_t k = 1; k <= extra; ++k) {
            if (i + k >= text.size() || (static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        }
        if (!ok) {
            out += static_cast<char16_t>(0xFFFD);
            ++i;
            continue;
        }
        i += extra + 1;
        if (code >= 0x10000) {
            code -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (code >> 10));
            out += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
        } else {
            out += static_cast<char16_t>(code);
        }
    }
    return out;
}

std::string fromUtf16(const std::u16string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t code = text[i];
        if (code >= 0xD800 && code < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
            code = 0x10000 + ((code - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return out;
}

bool isAscii(const std::string& text) {
    for (unsigned char c : text) {
        if (c >= 0x80) return false;
    }
    return true;
}

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();

std::wstring toWide(const std::u16string& text) {
    return std::wstring(text.begin(), text.end());
}

std::u16string fromWide(const std::wstring& text) {
    return std::u16string(text.begin(), text.end());
}

// A string's text and its UTF-16 units. A string value's are its own,
// whether it is ASCII (and its units if not) found the first time and
// kept with the value; any other text is converted here.
struct Text {
    std::string owned;
    std::u16string ownedUnits;
    const std::string& utf8;
    const std::u16string& units;
    bool ascii;
    explicit Text(Value* string)
        : utf8(string->stringValue), units(unitsOf(string)), ascii(string->textAscii == 1) {}
    explicit Text(std::string text) : owned(std::move(text)), utf8(owned), units(ownedUnits), ascii(isAscii(owned)) {
        if (!ascii) ownedUnits = toUtf16(utf8);
    }
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;
    size_t length() const { return ascii ? utf8.size() : units.size(); }
    std::string sub(size_t start, size_t end) const {
        if (end <= start) return std::string();
        if (ascii) return utf8.substr(start, end - start);
        return fromUtf16(units.substr(start, end - start));
    }
    char16_t at(size_t i) const { return ascii ? static_cast<char16_t>(utf8[i]) : units[i]; }
    std::u16string all16() const { return ascii ? std::u16string(utf8.begin(), utf8.end()) : units; }

private:
    static const std::u16string& unitsOf(Value* string) {
        if (string->textAscii < 0) {
            string->textAscii = isAscii(string->stringValue) ? 1 : 0;
        }
        if (string->textAscii) {
            static const std::u16string none;
            return none;
        }
        if (!string->textUnits) {
            string->textUnits = std::make_unique<std::u16string>(toUtf16(string->stringValue));
        }
        return *string->textUnits;
    }
};

Text textOf(VM& vm, const Val& self) {
    if (self.isString()) return Text(self.ref.get());
    return Text(vm.toString(self));
}

// A position argument as an integer (NaN 0, infinities kept large).
double integerArgument(const Val& value, double otherwise) {
    if (value.isUndefined()) return otherwise;
    const double n = value.toNumber();
    if (std::isnan(n)) return 0;
    return std::trunc(n);
}

size_t clamp(double value, size_t length) {
    if (value < 0) return 0;
    if (value > static_cast<double>(length)) return length;
    return static_cast<size_t>(value);
}

size_t findUnits(const std::u16string& haystack, const std::u16string& needle, size_t from) {
    if (from > haystack.size()) return std::u16string::npos;
    return haystack.find(needle, from);
}

// RegExp

struct RegExpState : HostObject {
    std::string source;
    bool global = false;
    bool ignoreCase = false;
    bool multiline = false;
    bool dotall = false;
    bool extended = false;
    std::shared_ptr<std::wregex> regex;
    std::vector<std::string> groupNames;
};

// The pattern made ready for std::regex: dotall dots, extended spacing and
// named groups worked in.
std::u16string preparePattern(const std::u16string& pattern, bool dotall, bool extended,
                              std::vector<std::string>& names) {
    std::u16string out;
    bool inClass = false;
    int group = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        const char16_t c = pattern[i];
        if (c == '\\' && i + 1 < pattern.size()) {
            out += c;
            out += pattern[++i];
            continue;
        }
        if (inClass) {
            if (c == ']') inClass = false;
            out += c;
            continue;
        }
        if (c == '[') {
            inClass = true;
            out += c;
            continue;
        }
        if (extended && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
            continue;
        }
        if (extended && c == '#') {
            while (i < pattern.size() && pattern[i] != '\n') ++i;
            continue;
        }
        if (dotall && c == '.') {
            out += u"[\\s\\S]";
            continue;
        }
        if (c == '(') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '?') {
                // (?P<name>...) and (?<name>...): a group by number here.
                size_t at = i + 2;
                if (at < pattern.size() && pattern[at] == 'P') ++at;
                if (at < pattern.size() && pattern[at] == '<' && at + 1 < pattern.size() && pattern[at + 1] != '=' &&
                    pattern[at + 1] != '!') {
                    const size_t close = pattern.find('>', at);
                    if (close != std::u16string::npos) {
                        ++group;
                        names.resize(static_cast<size_t>(group));
                        names[static_cast<size_t>(group - 1)] = fromUtf16(pattern.substr(at + 1, close - at - 1));
                        out += '(';
                        i = close;
                        continue;
                    }
                }
            } else {
                ++group;
            }
        }
        out += c;
    }
    return out;
}

std::shared_ptr<std::wregex> compileRegex(VM& vm, RegExpState& state) {
    static std::map<std::string, std::pair<std::shared_ptr<std::wregex>, std::vector<std::string>>> cache;
    const std::string key = state.source + "/" + (state.ignoreCase ? "i" : "") + (state.multiline ? "m" : "") +
                            (state.dotall ? "s" : "") + (state.extended ? "x" : "");
    auto found = cache.find(key);
    if (found != cache.end()) {
        state.groupNames = found->second.second;
        return found->second.first;
    }
    std::vector<std::string> names;
    const std::u16string pattern = preparePattern(toUtf16(state.source), state.dotall, state.extended, names);
    auto flags = std::regex_constants::ECMAScript;
    if (state.ignoreCase) flags |= std::regex_constants::icase;
    if (state.multiline) flags |= std::regex_constants::multiline;
    std::shared_ptr<std::wregex> regex;
    try {
        regex = std::make_shared<std::wregex>(toWide(pattern), flags);
    } catch (const std::regex_error& failed) {
        vm.throwError("SyntaxError", "Error #1100: Invalid regular expression /" + state.source + "/: " + failed.what(),
                      1100);
    }
    if (cache.size() > 500) cache.clear();
    cache[key] = {regex, names};
    state.groupNames = names;
    return regex;
}

RegExpState& regexpOf(VM& vm, const Val& self) {
    return hostOf<RegExpState>(vm, self, "RegExp");
}

Val makeRegExp(VM& vm, const std::string& source, const std::string& flags) {
    return vm.construct(vm.findDefinition("RegExp"), {Val::string(source), Val::string(flags)});
}

bool isRegExp(const Val& value) {
    return value.host<RegExpState>() != nullptr;
}

// One match: where (in units) and its groups.
struct Match {
    size_t index = 0;
    size_t length = 0;
    std::vector<bool> matched;
    std::vector<std::u16string> groups;
};

bool searchFrom(VM& vm, RegExpState& state, const std::wstring& subject, size_t from, Match& out) {
    if (!state.regex) state.regex = compileRegex(vm, state);
    if (from > subject.size()) return false;
    std::wsmatch match;
    auto flags = std::regex_constants::match_default;
    if (from > 0) flags |= std::regex_constants::match_prev_avail;
    if (!std::regex_search(subject.begin() + static_cast<long>(from), subject.end(), match, *state.regex, flags)) {
        return false;
    }
    out.index = from + static_cast<size_t>(match.position(0));
    out.length = static_cast<size_t>(match.length(0));
    out.groups.clear();
    out.matched.clear();
    for (size_t i = 0; i < match.size(); ++i) {
        out.matched.push_back(match[i].matched);
        out.groups.push_back(fromWide(match[i].str()));
    }
    return true;
}

Val matchArray(VM& vm, const RegExpState& state, const Match& match, const std::string& input) {
    std::vector<Val> elements;
    for (size_t i = 0; i < match.groups.size(); ++i) {
        elements.push_back(match.matched[i] ? Val::string(fromUtf16(match.groups[i])) : Val());
    }
    Val result = vm.newArray(std::move(elements));
    result->properties["index"] = Val::number(static_cast<double>(match.index));
    result->properties["input"] = Val::string(input);
    for (size_t i = 0; i < state.groupNames.size(); ++i) {
        if (!state.groupNames[i].empty() && i + 1 < match.groups.size()) {
            result->properties[state.groupNames[i]] = result->elements[i + 1];
        }
    }
    return result;
}

Val execRegExp(VM& vm, const Val& self, const std::string& input) {
    RegExpState& state = regexpOf(vm, self);
    const std::wstring subject = toWide(toUtf16(input));
    size_t from = 0;
    if (state.global) {
        const double last = vm.getProperty(self, "lastIndex").toNumber();
        if (last > static_cast<double>(subject.size())) {
            vm.setProperty(self, "lastIndex", Val::number(0));
            return Val::null();
        }
        from = last > 0 ? static_cast<size_t>(last) : 0;
    }
    Match match;
    if (!searchFrom(vm, state, subject, from, match)) {
        if (state.global) vm.setProperty(self, "lastIndex", Val::number(0));
        return Val::null();
    }
    if (state.global) {
        vm.setProperty(self, "lastIndex", Val::number(static_cast<double>(match.index + match.length)));
    }
    return matchArray(vm, state, match, input);
}

// Every match, for replace and match with g.
std::vector<Match> allMatches(VM& vm, RegExpState& state, const std::wstring& subject) {
    std::vector<Match> matches;
    size_t from = 0;
    while (from <= subject.size()) {
        Match match;
        if (!searchFrom(vm, state, subject, from, match)) break;
        matches.push_back(match);
        from = match.index + match.length;
        if (match.length == 0) ++from;
    }
    return matches;
}

// A replacement string with $&, $1, $` and $' worked in.
std::u16string expandReplacement(const std::u16string& replacement, const Match& match, const std::u16string& subject) {
    std::u16string out;
    for (size_t i = 0; i < replacement.size(); ++i) {
        const char16_t c = replacement[i];
        if (c != '$' || i + 1 >= replacement.size()) {
            out += c;
            continue;
        }
        const char16_t next = replacement[i + 1];
        if (next == '$') {
            out += '$';
            ++i;
        } else if (next == '&') {
            out += match.groups[0];
            ++i;
        } else if (next == '`') {
            out += subject.substr(0, match.index);
            ++i;
        } else if (next == '\'') {
            out += subject.substr(std::min(subject.size(), match.index + match.length));
            ++i;
        } else if (next >= '0' && next <= '9') {
            size_t group = static_cast<size_t>(next - '0');
            size_t used = 1;
            if (i + 2 < replacement.size() && replacement[i + 2] >= '0' && replacement[i + 2] <= '9') {
                const size_t two = group * 10 + static_cast<size_t>(replacement[i + 2] - '0');
                if (two < match.groups.size()) {
                    group = two;
                    used = 2;
                }
            }
            if (group > 0 && group < match.groups.size()) {
                out += match.groups[group];
                i += used;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

std::u16string replacementFor(VM& vm, const Val& replacement, const Match& match, const std::u16string& subject) {
    if (replacement.isFunction()) {
        std::vector<Val> args;
        for (size_t i = 0; i < match.groups.size(); ++i) {
            args.push_back(match.matched[i] ? Val::string(fromUtf16(match.groups[i])) : Val());
        }
        args.push_back(Val::number(static_cast<double>(match.index)));
        args.push_back(Val::string(fromUtf16(subject)));
        return toUtf16(vm.toString(vm.callFunction(replacement, Val::null(), args)));
    }
    return expandReplacement(toUtf16(vm.toString(replacement)), match, subject);
}

// Upper and lower case for the alphabets most text uses: Latin (with
// Latin-1 and Latin Extended-A), Greek and Cyrillic.
char16_t upperOf(char16_t c) {
    if (c >= 'a' && c <= 'z') return static_cast<char16_t>(c - 32);
    if (c < 0xB5) return c;
    if (c == 0xB5) return 0x39C;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return static_cast<char16_t>(c - 32);
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x137 && (c & 1)) return static_cast<char16_t>(c - 1);
    if (c >= 0x139 && c <= 0x148 && !(c & 1)) return static_cast<char16_t>(c - 1);
    if (c >= 0x14A && c <= 0x177 && (c & 1)) return static_cast<char16_t>(c - 1);
    if (c >= 0x179 && c <= 0x17E && !(c & 1)) return static_cast<char16_t>(c - 1);
    if (c == 0x3C2) return 0x3A3;
    if (c >= 0x3B1 && c <= 0x3C9) return static_cast<char16_t>(c - 32);
    if (c >= 0x430 && c <= 0x44F) return static_cast<char16_t>(c - 32);
    if (c >= 0x450 && c <= 0x45F) return static_cast<char16_t>(c - 80);
    return c;
}

char16_t lowerOf(char16_t c) {
    if (c >= 'A' && c <= 'Z') return static_cast<char16_t>(c + 32);
    if (c < 0xC0) return c;
    if (c <= 0xDE && c != 0xD7) return static_cast<char16_t>(c + 32);
    if (c == 0x178) return 0xFF;
    if (c >= 0x100 && c <= 0x137 && !(c & 1)) return static_cast<char16_t>(c + 1);
    if (c >= 0x139 && c <= 0x148 && (c & 1)) return static_cast<char16_t>(c + 1);
    if (c >= 0x14A && c <= 0x177 && !(c & 1)) return static_cast<char16_t>(c + 1);
    if (c >= 0x179 && c <= 0x17E && (c & 1)) return static_cast<char16_t>(c + 1);
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return static_cast<char16_t>(c + 32);
    if (c >= 0x410 && c <= 0x42F) return static_cast<char16_t>(c + 32);
    if (c >= 0x400 && c <= 0x40F) return static_cast<char16_t>(c + 80);
    return c;
}

std::u16string caseMapped(const std::u16string& text, bool upper) {
    std::u16string out = text;
    for (char16_t& c : out) {
        c = upper ? upperOf(c) : lowerOf(c);
    }
    return out;
}

} // namespace

void defineStringBuiltins(VM& vm) {
    VM::Builtins& core = vm.builtins();

    NativeClassBuilder string = vm.defineNativeClass("String");
    string.final()
        .constructor([](VM& vm, const Val&, Args args) {
            return Val::string(args.empty() ? std::string() : vm.toString(args[0]));
        })
        .callHandler([](VM& vm, const Val&, Args args) {
            return Val::string(args.empty() ? std::string() : vm.toString(args[0]));
        })
        .staticMethod("fromCharCode",
                      [](VM& vm, const Val&, Args args) {
                          std::u16string units;
                          for (const Val& arg : args) units += static_cast<char16_t>(toUint32(vm.toNumber(arg)) & 0xFFFF);
                          return Val::string(fromUtf16(units));
                      },
                      1)
        .getter("length", [](VM& vm, const Val& self, Args) {
            // Whether a string is ASCII is found once and kept with it, not
            // looked for at each read (s.length in a loop's test).
            return Val::number(static_cast<double>(textOf(vm, self).length()));
        })
        .method("charAt",
                [](VM& vm, const Val& self, Args args) {
                    const Text text = textOf(vm, self);
                    const double index = integerArgument(args[0], 0);
                    if (index < 0 || index >= static_cast<double>(text.length())) return Val::string("");
                    const size_t i = static_cast<size_t>(index);
                    return Val::string(text.sub(i, i + 1));
                },
                1)
        .method("charCodeAt",
                [](VM& vm, const Val& self, Args args) {
                    const Text text = textOf(vm, self);
                    const double index = integerArgument(args[0], 0);
                    if (index < 0 || index >= static_cast<double>(text.length())) return Val::number(kNaN);
                    return Val::number(text.at(static_cast<size_t>(index)));
                },
                1)
        .method("indexOf",
                [](VM& vm, const Val& self, Args args) {
                    const Text text = textOf(vm, self);
                    const std::string needle = args.empty() ? "undefined" : vm.toString(args[0]);
                    const size_t from = clamp(integerArgument(args[1], 0), text.length());
                    size_t found;
                    if (text.ascii && isAscii(needle)) {
                        found = text.utf8.find(needle, from);
                    } else {
                        found = findUnits(text.all16(), toUtf16(needle), from);
                    }
                    return Val::number(found == std::string::npos ? -1.0 : static_cast<double>(found));
                },
                1)
        .method("lastIndexOf",
                [](VM& vm, const Val& self, Args args) {
                    const Text text = textOf(vm, self);
                    const std::u16string needle = toUtf16(args.empty() ? "undefined" : vm.toString(args[0]));
                    const double from = integerArgument(args[1], static_cast<double>(text.length()));
                    const std::u16string all = text.all16();
                    const size_t found = all.rfind(needle, clamp(from, all.size()));
                    return Val::number(found == std::u16string::npos ? -1.0 : static_cast<double>(found));
                },
                1)
        .method("substr",
                [](VM& vm, const Val& self, Args args) {
                    const Text text = textOf(vm, self);
                    const size_t length = text.length();
                    double start = integerArgument(args[0], 0);
                    if (start < 0) start = std::max(0.0, start + static_cast<double>(length));
                    const size_t from = clamp(start, length);
                    const double count = integerArgument(args[1], static_cast<double>(length));
                    const size_t to = clamp(static_cast<double>(from) + std::max(0.0, count), length);
                    return Val::string(text.sub(from, to));
                },
                2)
        .method("substring",
                [](VM& vm, const Val& self, Args args) {
                    const Text text = textOf(vm, self);
                    const size_t length = text.length();
                    size_t a = clamp(integerArgument(args[0], 0), length);
                    size_t b = clamp(integerArgument(args[1], static_cast<double>(length)), length);
                    if (a > b) std::swap(a, b);
                    return Val::string(text.sub(a, b));
                },
                2)
        .method("slice",
                [](VM& vm, const Val& self, Args args) {
                    const Text text = textOf(vm, self);
                    const double length = static_cast<double>(text.length());
                    double a = integerArgument(args[0], 0);
                    double b = integerArgument(args[1], length);
                    if (a < 0) a += length;
                    if (b < 0) b += length;
                    const size_t from = clamp(a, text.length());
                    const size_t to = clamp(b, text.length());
                    return Val::string(to > from ? text.sub(from, to) : std::string());
                },
                2)
        .method("split",
                [](VM& vm, const Val& self, Args args) {
                    const Text text = textOf(vm, self);
                    const double limitValue = args.size() > 1 && !args[1].isUndefined() ? args[1].toNumber() : 4294967295.0;
                    const size_t limit = limitValue < 0 ? 4294967295u : static_cast<size_t>(toUint32(limitValue));
                    std::vector<Val> parts;
                    if (args.empty() || args[0].isUndefined()) {
                        parts.push_back(Val::string(text.utf8));
                        return vm.newArray(std::move(parts));
                    }
                    const std::u16string all = text.all16();
                    if (isRegExp(args[0])) {
                        RegExpState& state = regexpOf(vm, args[0]);
                        const std::wstring subject = toWide(all);
                        size_t last = 0;
                        size_t from = 0;
                        while (from < subject.size() && parts.size() < limit) {
                            Match match;
                            if (!searchFrom(vm, state, subject, from, match)) break;
                            if (match.length == 0 && match.index >= subject.size()) break;
                            if (match.length == 0 && match.index == last) {
                                from = match.index + 1;
                                if (from > subject.size()) break;
                                // An empty match splits between units.
                                if (match.index < subject.size()) {
                                    parts.push_back(Val::string(fromUtf16(all.substr(last, match.index + 1 - last))));
                                    last = match.index + 1;
                                }
                                continue;
                            }
                            parts.push_back(Val::string(fromUtf16(all.substr(last, match.index - last))));
                            for (size_t g = 1; g < match.groups.size() && parts.size() < limit; ++g) {
                                parts.push_back(match.matched[g] ? Val::string(fromUtf16(match.groups[g])) : Val());
                            }
                            last = match.index + match.length;
                            from = match.length == 0 ? last + 1 : last;
                        }
                        if (parts.size() < limit) parts.push_back(Val::string(fromUtf16(all.substr(last))));
                        return vm.newArray(std::move(parts));
                    }
                    const std::u16string separator = toUtf16(vm.toString(args[0]));
                    if (separator.empty()) {
                        for (size_t i = 0; i < all.size() && parts.size() < limit; ++i) {
                            parts.push_back(Val::string(fromUtf16(all.substr(i, 1))));
                        }
                        return vm.newArray(std::move(parts));
                    }
                    size_t start = 0;
                    while (parts.size() < limit) {
                        const size_t found = all.find(separator, start);
                        if (found == std::u16string::npos) {
                            parts.push_back(Val::string(fromUtf16(all.substr(start))));
                            break;
                        }
                        parts.push_back(Val::string(fromUtf16(all.substr(start, found - start))));
                        start = found + separator.size();
                    }
                    return vm.newArray(std::move(parts));
                },
                2)
        .method("replace",
                [](VM& vm, const Val& self, Args args) {
                    const Text text = textOf(vm, self);
                    const std::u16string subject = text.all16();
                    if (isRegExp(args[0])) {
                        RegExpState& state = regexpOf(vm, args[0]);
                        const std::wstring wide = toWide(subject);
                        std::vector<Match> matches;
                        if (state.global) {
                            matches = allMatches(vm, state, wide);
                        } else {
                            Match match;
                            if (searchFrom(vm, state, wide, 0, match)) matches.push_back(match);
                        }
                        if (matches.empty()) return Val::string(text.utf8);
                        std::u16string out;
                        size_t last = 0;
                        for (const Match& match : matches) {
                            out += subject.substr(last, match.index - last);
                            out += replacementFor(vm, args[1], match, subject);
                            last = match.index + match.length;
                        }
                        out += subject.substr(std::min(last, subject.size()));
                        return Val::string(fromUtf16(out));
                    }
                    const std::u16string pattern = toUtf16(vm.toString(args[0]));
                    const size_t found = subject.find(pattern);
                    if (found == std::u16string::npos) return Val::string(text.utf8);
                    Match match;
                    match.index = found;
                    match.length = pattern.size();
                    match.groups.push_back(pattern);
                    match.matched.push_back(true);
                    std::u16string out = subject.substr(0, found) + replacementFor(vm, args[1], match, subject) +
                                         subject.substr(found + pattern.size());
                    return Val::string(fromUtf16(out));
                },
                2)
        .method("match",
                [](VM& vm, const Val& self, Args args) {
                    const Text text = textOf(vm, self);
                    Val regexp = isRegExp(args[0]) ? args[0] : makeRegExp(vm, vm.toString(args[0]), "");
                    RegExpState& state = regexpOf(vm, regexp);
                    if (!state.global) {
                        return execRegExp(vm, regexp, text.utf8);
                    }
                    const std::wstring wide = toWide(text.all16());
                    std::vector<Val> found;
                    for (const Match& match : allMatches(vm, state, wide)) {
                        found.push_back(Val::string(fromUtf16(match.groups[0])));
                    }
                    vm.setProperty(regexp, "lastIndex", Val::number(0));
                    if (found.empty()) return Val::null();
                    return vm.newArray(std::move(found));
                },
                1)
        .method("search",
                [](VM& vm, const Val& self, Args args) {
                    const Text text = textOf(vm, self);
                    Val regexp = isRegExp(args[0]) ? args[0] : makeRegExp(vm, vm.toString(args[0]), "");
                    Match match;
                    if (!searchFrom(vm, regexpOf(vm, regexp), toWide(text.all16()), 0, match)) {
                        return Val::number(-1);
                    }
                    return Val::number(static_cast<double>(match.index));
                },
                1)
        .method("toUpperCase",
                [](VM& vm, const Val& self, Args) {
                    return Val::string(fromUtf16(caseMapped(textOf(vm, self).all16(), true)));
                })
        .method("toLowerCase",
                [](VM& vm, const Val& self, Args) {
                    return Val::string(fromUtf16(caseMapped(textOf(vm, self).all16(), false)));
                })
        .method("toLocaleUpperCase",
                [](VM& vm, const Val& self, Args) {
                    return Val::string(fromUtf16(caseMapped(textOf(vm, self).all16(), true)));
                })
        .method("toLocaleLowerCase",
                [](VM& vm, const Val& self, Args) {
                    return Val::string(fromUtf16(caseMapped(textOf(vm, self).all16(), false)));
                })
        .method("concat",
                [](VM& vm, const Val& self, Args args) {
                    std::string out = textOf(vm, self).utf8;
                    for (const Val& arg : args) out += vm.toString(arg);
                    return Val::string(out);
                },
                1)
        .method("localeCompare",
                [](VM& vm, const Val& self, Args args) {
                    const std::u16string a = textOf(vm, self).all16();
                    const std::u16string b = toUtf16(args.empty() ? "undefined" : vm.toString(args[0]));
                    return Val::number(a < b ? -1 : (a > b ? 1 : 0));
                },
                1)
        .method("toString", [](VM& vm, const Val& self, Args) { return Val::string(textOf(vm, self).utf8); })
        .method("valueOf", [](VM& vm, const Val& self, Args) { return Val::string(textOf(vm, self).utf8); });
    core.string = string.define()->cls;

    Val regexp =
        vm.defineNativeClass("RegExp")
            .dynamic()
            .field("lastIndex", Val::number(0))
            .constructor([](VM& vm, const Val& self, Args args) {
                auto state = std::make_shared<RegExpState>();
                std::string flags;
                if (const RegExpState* other = args[0].host<RegExpState>()) {
                    state->source = other->source;
                    flags = std::string(other->global ? "g" : "") + (other->ignoreCase ? "i" : "") +
                            (other->multiline ? "m" : "") + (other->dotall ? "s" : "") + (other->extended ? "x" : "");
                } else {
                    state->source = args.empty() || args[0].isUndefined() ? std::string() : vm.toString(args[0]);
                }
                if (args.size() > 1 && !args[1].isUndefined()) flags = vm.toString(args[1]);
                for (char flag : flags) {
                    switch (flag) {
                        case 'g': state->global = true; break;
                        case 'i': state->ignoreCase = true; break;
                        case 'm': state->multiline = true; break;
                        case 's': state->dotall = true; break;
                        case 'x': state->extended = true; break;
                        default: break;
                    }
                }
                state->regex = compileRegex(vm, *state);
                self->host = state;
                return Val();
            })
            .callHandler([](VM& vm, const Val& cls, Args args) {
                if (args[0].host<RegExpState>() && args.size() < 2) return args[0];
                return vm.construct(cls, std::vector<Val>(args.begin(), args.end()));
            })
            .getter("source", [](VM& vm, const Val& self, Args) { return Val::string(regexpOf(vm, self).source); })
            .getter("global", [](VM& vm, const Val& self, Args) { return Val::boolean(regexpOf(vm, self).global); })
            .getter("ignoreCase",
                    [](VM& vm, const Val& self, Args) { return Val::boolean(regexpOf(vm, self).ignoreCase); })
            .getter("multiline",
                    [](VM& vm, const Val& self, Args) { return Val::boolean(regexpOf(vm, self).multiline); })
            .getter("dotall", [](VM& vm, const Val& self, Args) { return Val::boolean(regexpOf(vm, self).dotall); })
            .getter("extended",
                    [](VM& vm, const Val& self, Args) { return Val::boolean(regexpOf(vm, self).extended); })
            .method("exec",
                    [](VM& vm, const Val& self, Args args) {
                        return execRegExp(vm, self, args.empty() ? "undefined" : vm.toString(args[0]));
                    },
                    1)
            .method("test",
                    [](VM& vm, const Val& self, Args args) {
                        return Val::boolean(
                            !execRegExp(vm, self, args.empty() ? "undefined" : vm.toString(args[0])).isNull());
                    },
                    1)
            .prototypeMethod("toString",
                             [](VM& vm, const Val& self, Args) {
                                 const RegExpState& state = regexpOf(vm, self);
                                 return Val::string("/" + state.source + "/" + (state.global ? "g" : "") +
                                                    (state.ignoreCase ? "i" : "") + (state.multiline ? "m" : "") +
                                                    (state.dotall ? "s" : "") + (state.extended ? "x" : ""));
                             })
            .define();
    core.regexp = regexp->cls;
}

} // namespace as3
