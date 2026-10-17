#include "js_writer.h"

#include <cstdio>
#include <cstdlib>
#include <set>

namespace as3 {
namespace js {

namespace {

const char kMarkStart = '\x01';
const char kMarkEnd = '\x02';

void vlq(std::string& out, int value) {
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned int bits = value < 0 ? (static_cast<unsigned int>(-value) << 1) | 1u : static_cast<unsigned int>(value) << 1;
    do {
        unsigned int digit = bits & 31u;
        bits >>= 5;
        if (bits) digit |= 32u;
        out += digits[digit];
    } while (bits);
}

} // namespace

std::string mark(int sourceLine) {
    if (sourceLine <= 0) return std::string();
    return std::string(1, kMarkStart) + std::to_string(sourceLine) + kMarkEnd;
}

void Lines::add(const std::string& text) {
    const std::string pad(static_cast<size_t>(_depth) * 4, ' ');
    size_t start = 0;
    for (;;) {
        const size_t end = text.find('\n', start);
        const std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        // A line of only marks is no line.
        bool content = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == kMarkStart) {
                const size_t close = line.find(kMarkEnd, i);
                if (close == std::string::npos) break;
                i = close;
            } else if (line[i] != ' ') {
                content = true;
                break;
            }
        }
        if (content) _lines.push_back(pad + line);
        else if (line.empty() && end != std::string::npos) _lines.push_back(std::string());
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

void Lines::blank() {
    if (!_lines.empty() && !_lines.back().empty()) _lines.push_back(std::string());
}

void Lines::append(const Lines& other) {
    const std::string pad(static_cast<size_t>(_depth) * 4, ' ');
    for (const std::string& line : other._lines) _lines.push_back(line.empty() ? line : pad + line);
}

std::string Lines::text() const {
    std::string out;
    for (size_t i = 0; i < _lines.size(); ++i) {
        if (i) out += '\n';
        out += _lines[i];
    }
    return out;
}

std::string indented(const std::string& text, int depth) {
    Lines lines;
    for (int i = 0; i < depth; ++i) lines.indent();
    lines.add(text);
    return lines.text();
}

std::string unmark(const std::string& text, std::vector<int>& sourceLines) {
    std::string out;
    out.reserve(text.size());
    int current = 0;
    bool marked = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == kMarkStart) {
            const size_t close = text.find(kMarkEnd, i);
            if (close == std::string::npos) break;
            const int line = std::atoi(text.substr(i + 1, close - i - 1).c_str());
            if (!marked) {
                current = line;
                marked = true;
            }
            i = close;
            continue;
        }
        if (c == '\n') {
            sourceLines.push_back(marked ? current : 0);
            marked = false;
        }
        out += c;
    }
    sourceLines.push_back(marked ? current : 0);
    return out;
}

std::string sourceMap(const std::string& file, const std::vector<std::string>& sources,
                      const std::vector<int>& sourceOf, const std::vector<int>& sourceLines) {
    std::string mappings;
    int lastSource = 0;
    int lastLine = 0;
    for (size_t i = 0; i < sourceLines.size(); ++i) {
        if (i) mappings += ';';
        if (sourceLines[i] <= 0) continue;
        const int source = i < sourceOf.size() ? sourceOf[i] : 0;
        const int line = sourceLines[i] - 1;
        vlq(mappings, 0);
        vlq(mappings, source - lastSource);
        vlq(mappings, line - lastLine);
        vlq(mappings, 0);
        lastSource = source;
        lastLine = line;
    }
    std::string out = "{\"version\":3,\"file\":" + quote(file) + ",\"sources\":[";
    for (size_t i = 0; i < sources.size(); ++i) out += (i ? "," : "") + quote(sources[i]);
    out += "],\"names\":[],\"mappings\":\"" + mappings + "\"}";
    return out;
}

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                    out += buffer;
                } else if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                           (static_cast<unsigned char>(text[i + 2]) == 0xA8 ||
                            static_cast<unsigned char>(text[i + 2]) == 0xA9)) {
                    // U+2028 and U+2029 end lines in older engines.
                    out += static_cast<unsigned char>(text[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
                    i += 2;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out + "\"";
}

bool isReserved(const std::string& text) {
    static const std::set<std::string> words = {
        "await", "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do",
        "else", "enum", "export", "extends", "false", "finally", "for", "function", "if", "implements", "import",
        "in", "instanceof", "interface", "let", "new", "null", "package", "private", "protected", "public",
        "return", "static", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while",
        "with", "yield", "eval", "arguments",
    };
    return words.count(text) > 0;
}

bool isIdentifier(const std::string& text) {
    if (text.empty()) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80;
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !(digit && i > 0)) return false;
    }
    return true;
}

} // namespace js
} // namespace as3
