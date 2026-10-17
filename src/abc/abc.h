#pragma once

// Compiling ActionScript 3 to ABC (AVM2 bytecode, the abcFile a SWF's
// DoABC tag carries), for movies that run in a player: classes in
// packages, and the frame scripts of timelines as the methods of a
// MovieClip subclass, as published movies have them.
//
//   as3::abc::Output out = as3::abc::compile(sources, timelines);
//   if (out.ok) { /* out.abc into a DoABC tag, out.classes for SymbolClass */ }
//   else std::cerr << out.errorText();
//
// Classes the player provides (flash.*, and fl.* when the movie has
// them) need no definitions: what is imported is named by its package.

#include "abc_file.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace as3 {
namespace abc {

// A source file: a package with its classes, interfaces, functions and
// variables, and code outside the package.
struct Source {
    std::string text;
    std::string fileName;
};

// A named instance on a timeline's stage: a public variable of the
// timeline's class, set by the player when the instance is placed.
struct StageInstance {
    std::string name;
    // Its class, qualified ("flash.display.SimpleButton",
    // "fl.controls.CheckBox"); empty for an untyped variable.
    std::string type;
};

// One frame's script; frames count from 0.
struct FrameScript {
    int frame = 0;
    std::string code;
};

// A timeline with scripts: compiled to a class whose constructor calls
// addFrameScript for each frame. Variables and functions at the top of a
// frame's script are the class's (public) members, and each frame's code
// is a method, frame1 for frame 0 and so on. As in the authoring tool, the
// flash.* packages are imported without import statements. A library
// symbol exported for ActionScript without a class of its own is a
// Timeline without frames.
struct Timeline {
    // Qualified: "bunnyPaint_fla.MainTimeline", "bunnyPaint_fla.Button_3".
    std::string className;
    std::string baseClass = "flash.display.MovieClip";
    std::vector<FrameScript> frames;
    std::vector<StageInstance> instances;
};

// The name a timeline's class has in movies published from a document:
// "<document>_fla.MainTimeline" for the main timeline (an empty symbol),
// "<document>_fla.<symbol>_<index>" for a symbol's; in the document's name
// a character that cannot be in a name becomes _, in a symbol's it is left
// out ("Button 1" is Button1_3).
std::string timelineClassName(const std::string& documentName, const std::string& symbolName = std::string(),
                              int index = 0);

struct Options {
    // debugfile and debugline instructions, for a debugger player's stack
    // traces and line numbers.
    bool debugInfo = false;
    // Conditional compilation constants: "CONFIG::debug" = "true".
    std::map<std::string, std::string> constants;
    // include "file.as": the file's source, or nullopt when there is none.
    std::function<std::optional<std::string>(const std::string& path)> includeHandler;
    // Checks the result with verify() (abc_verifier.h); what it finds is
    // an error.
    bool verify = true;
};

struct Diagnostic {
    bool isError = true;
    std::string fileName;
    int line = 0;
    std::string message;

    // "Main.as:12: error: ..."
    std::string text() const;
};

struct Output {
    bool ok = false;
    // The abcFile, for a DoABC tag; empty when there were errors.
    std::vector<uint8_t> abc;
    // Every class defined, qualified with dots ("com.example.Main"), in the
    // order defined; timelines' classes last.
    std::vector<std::string> classes;
    std::vector<Diagnostic> diagnostics;

    // The errors (and warnings), a line each.
    std::string errorText() const;
};

Output compile(const std::vector<Source>& sources, const std::vector<Timeline>& timelines = {},
               const Options& options = Options());

} // namespace abc
} // namespace as3
