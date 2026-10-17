#pragma once

// What the translator writes JavaScript with: text that carries, at the
// start of its lines, the .as lines they come from (marks), written out
// with its source map.

#include <string>
#include <vector>

namespace as3 {
namespace js {

// A mark for a line of the source: put before code, it maps the generated
// line the code starts on to that line (from 1).
std::string mark(int sourceLine);

// Lines of code at a depth: each line given is indented to it (the lines
// of a text with several, each), keeping its marks.
class Lines {
public:
    void add(const std::string& text);
    void add(const std::string& text, int sourceLine) { add(mark(sourceLine) + text); }
    void blank();
    void indent() { ++_depth; }
    void dedent() { --_depth; }
    void append(const Lines& other);
    bool empty() const { return _lines.empty(); }
    // The lines, joined, without a last newline.
    std::string text() const;
    const std::vector<std::string>& lines() const { return _lines; }

private:
    std::vector<std::string> _lines;
    int _depth = 0;
};

// Each line of a text indented by depth levels.
std::string indented(const std::string& text, int depth);

// The text without its marks, and for each of its lines the source line
// its first mark gives (0 for none).
std::string unmark(const std::string& text, std::vector<int>& sourceLines);

// A source map (v3) for a generated file whose lines map, through
// sourceLines (one for each generated line, from its first), to lines of
// the sources: sourceOf[i] is the index of line i's source in sources.
std::string sourceMap(const std::string& file, const std::vector<std::string>& sources,
                      const std::vector<int>& sourceOf, const std::vector<int>& sourceLines);

// A JavaScript string literal.
std::string quote(const std::string& text);

// Whether text is a JavaScript identifier (and not a reserved word, when
// strict), or a name usable after a dot.
bool isIdentifier(const std::string& text);
bool isReserved(const std::string& text);

} // namespace js
} // namespace as3
