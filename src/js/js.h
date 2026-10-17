#pragma once

// Translating ActionScript 3 to JavaScript, for movies published to
// HTML5: classes in packages and the frame scripts of timelines become
// readable JavaScript (ES2020 classes) that runs on a JavaScript
// implementation of the flash.* classes, with as3.js (runtime()) for what
// the language needs beyond JavaScript's.
//
//   as3::js::Output out = as3::js::translate(sources, timelines);
//   if (out.ok) { /* runtime(), the flash.* runtime, then out.bundle */ }
//   else std::cerr << out.errorText();
//
// The sources and timelines are as3::abc's: what is compiled to ABC for a
// player translates to JavaScript for a page. The README describes the
// JavaScript made and the runtime interface it targets.

#include "../abc/abc.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace as3 {
namespace js {

using abc::Diagnostic;
using abc::FrameScript;
using abc::Source;
using abc::StageInstance;
using abc::Timeline;
using abc::timelineClassName;

struct Options {
    // Conditional compilation constants: "CONFIG::debug" = "true".
    std::map<std::string, std::string> constants;
    // include "file.as": the file's source, or nullopt when there is none.
    std::function<std::optional<std::string>(const std::string& path)> includeHandler;
    // A source map (v3) for each file and the bundle, back to the .as lines.
    bool sourceMaps = true;
    // The sources' comments kept, those on lines of their own.
    bool comments = true;
    // Typed parameters converted on entry as ActionScript converts them:
    // int and uint always (x |= 0), Number, String and Boolean too when set.
    bool coerceAllParameters = false;
};

struct Output {
    bool ok = false;
    // Each source's (and timeline's) JavaScript, by path: a package's
    // class at its package's path ("com/example/Ball.js"), other sources
    // by their own names ("main.js").
    std::map<std::string, std::string> files;
    // Every file in one script, in the order given, ending by running the
    // sources' code outside their packages (as3.runScripts()).
    std::string bundle;
    // The source maps: each file's ("com/example/Ball.js.map"), and
    // "bundle.js.map".
    std::map<std::string, std::string> sourceMaps;
    // Every class defined, qualified with dots ("com.example.Main"), in the
    // order defined; timelines' classes last.
    std::vector<std::string> classes;
    std::vector<Diagnostic> diagnostics;

    // The errors (and warnings), a line each.
    std::string errorText() const;
};

Output translate(const std::vector<Source>& sources, const std::vector<Timeline>& timelines = {},
                 const Options& options = Options());

// as3.js: the runtime helpers translated code calls (as3.define, as3.is,
// Vector, Dictionary, XML...), to load before the flash.* runtime and the
// translated code.
const std::string& runtime();

} // namespace js
} // namespace as3
