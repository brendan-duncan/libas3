#pragma once

// Translating ActionScript 1 and 2 to JavaScript, for movies published to
// HTML5: a movie's frame scripts, the on() and onClipEvent() handlers of its
// instances, #initclip code and ActionScript 2 classes become one
// readable JavaScript module that runs on avm1.js (runtime()), which gives
// the language as the first ActionScript virtual machine (AVM1) has it, and
// a host that gives movie clips their ActionScript 2 face.
//
//   as3::js::as2::Movie movie;
//   movie.frames.push_back({"", 0, "stop();\nhat._visible = false;", "Scene 1", "Scene 1, frame 1"});
//   as3::js::as2::Output out = as3::js::as2::translate(movie);
//   // out.code: avm1.movie(function ($, movie) { ... });
//
// The README ("ActionScript 1 and 2 to JavaScript") describes the module
// and what the host gives.

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace as3 {
namespace js {
namespace as2 {

struct Options {
    // The SWF version the movie is published as: from 7 names are case
    // sensitive and undefined is "undefined" in strings (NaN in numbers);
    // before, names are found whatever their case and undefined is "" (0).
    int swfVersion = 8;
    // include "file.as": the file's source, or nullopt when there is none.
    std::function<std::optional<std::string>(const std::string& path)> includeHandler;
};

// A frame's script: on the main timeline (timeline "") or a symbol's (its
// library name), the frame from 0. A main timeline's script knows its
// scene, as gotoAndPlay(5) there is the scene's fifth frame.
struct FrameScript {
    std::string timeline;
    int frame = 0;
    std::string source;
    std::string scene;
    // Where it is, for messages: "Scene 1, layer actions, frame 1".
    std::string where;
};

// An instance's script: its on() and onClipEvent() handlers. The id is the
// host's, to find the instance by.
struct InstanceScript {
    int id = 0;
    std::string source;
    std::string where;
};

// An ActionScript 2 class file.
struct ClassFile {
    std::string source;
    std::string fileName;
};

struct Movie {
    std::vector<ClassFile> classes;
    std::vector<FrameScript> frames;
    std::vector<InstanceScript> instances;
    // Library symbols linked to classes: the linkage name and the class's
    // qualified name, as Object.registerClass(linkage, class) has them.
    std::vector<std::pair<std::string, std::string>> registrations;
};

struct Output {
    // False when a script did not translate; the rest is in code all the
    // same, without it.
    bool ok = false;
    // The module: avm1.movie(function ($, movie) { ... });
    std::string code;
    // "Scene 1, frame 1, line 3: what", a line each.
    std::vector<std::string> errors;
    // The classes defined, qualified.
    std::vector<std::string> classes;

    std::string errorText() const;
};

Output translate(const Movie& movie, const Options& options = Options());

// avm1.js: the helpers translated code calls, to load before the host's
// layer and the module.
const std::string& runtime();

} // namespace as2
} // namespace js
} // namespace as3
