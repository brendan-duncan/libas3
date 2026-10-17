#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace as3 {

class Program;

// ActionScript 1 and 2 compiled to the action bytecode SWF movies carry
// for Flash Player's first virtual machine (AVM1): frame scripts for
// DoAction and DoInitAction tags, the on() handlers of buttons and the
// onClipEvent() handlers of movie clips, and ActionScript 2 classes. The
// source is read by the same parser as ActionScript 3, which knows the
// older forms (on, onClipEvent, tellTarget, ifFrameLoaded).
namespace avm1 {

// The members an ActionScript 2 class declares: its methods' code finds
// them by their bare names (name for this.name, count for Hat.count), and so
// does a class that extends it.
struct ClassMembers {
    std::string superName;
    std::vector<std::string> instance;
    std::vector<std::string> statics;
};

// What changes the code a script compiles to, beyond the script itself.
struct Options {
    // The scenes of the movie's main timeline, by name, and the frame (from
    // 0) of the main timeline each starts on, for gotoAndPlay("Scene 2", 1).
    std::map<std::string, int> scenes;
    // Where the scene the code is in starts on the main timeline, as frame
    // numbers there count from it; -1 when the code is not on the main
    // timeline. And where the scenes after and before it start, -1 for
    // none, for nextScene() and prevScene().
    int sceneStart = -1;
    int nextScene = -1;
    int prevScene = -1;
    // The ActionScript 2 classes the movie has, by qualified name
    // ("com.studio.Hat"), so that a class's short name in a class of the
    // same package, or one imported by "com.studio.*", finds it.
    std::vector<std::string> classes;
    // Their members, by qualified name, for the names a class inherits (see
    // Compiler::classMembers). MovieClip's own are known without.
    std::map<std::string, ClassMembers> members;
};

// The handlers an instance's script gives: the events as written ("release",
// "keyPress <Enter>", "enterFrame"), whether they were onClipEvent() rather
// than on(), and the code that runs, ending with ActionEnd.
struct Handler {
    std::vector<std::string> events;
    bool clipEvent = false;
    std::vector<uint8_t> actions;
    int line = 0;
};

// A frame's script: its actions for a DoAction tag, and what it has between
// #initclip and #endinitclip, for a DoInitAction tag; each ends with
// ActionEnd, and is empty when there is nothing of that kind.
struct FrameScript {
    std::vector<uint8_t> actions;
    std::vector<uint8_t> initActions;
};

// An ActionScript 2 class as its file defines it: its qualified name, the
// qualified name of the class it extends (empty for none), the other
// classes it needs defined first, and the code that defines it, for a
// DoInitAction tag.
struct ClassCode {
    std::string name;
    std::string superName;
    std::vector<std::string> needs;
    std::vector<uint8_t> actions;
};

class Compiler {
public:
    explicit Compiler(const Options& options = Options());

    // A frame's script. On an error the result is empty and errors() says
    // why.
    FrameScript compileFrameScript(const std::string& source, const std::string& fileName = std::string());
    // Code that is a script of its own, as a DoAction's.
    std::vector<uint8_t> compileScript(const std::string& source, const std::string& fileName = std::string());
    // The on() and onClipEvent() handlers of an instance's script.
    std::vector<Handler> compileHandlers(const std::string& source, const std::string& fileName = std::string());
    // An ActionScript 2 class file (or an interface's). False on an error.
    bool compileClass(const std::string& source, ClassCode& out, const std::string& fileName = std::string());
    // The qualified name of the class a class file defines and the members
    // it declares, for Options::members. False when it does not parse.
    bool classMembers(const std::string& source, std::string& name, ClassMembers& out,
                      const std::string& fileName = std::string());

    // The errors of the last compile, a line each: "file, line N: what".
    const std::vector<std::string>& errors() const { return _errors; }
    bool hasErrors() const { return !_errors.empty(); }

private:
    Options _options;
    std::vector<std::string> _errors;

    std::unique_ptr<Program> parse(const std::string& source, const std::string& fileName);
};

// The SWF tag body of a DoAction (the actions as they are), for reading.
// A line an action, with jump targets as labels and the bodies of
// functions, with blocks and try blocks indented:
//
//     ConstantPool "a" "trace"
//     Push "a" 1
//     DefineLocal
//     Push "a"
//     GetVariable
//     Trace
//
// Strings are given quoted (a constant pool entry by its text), integers
// plainly, doubles with a decimal point, registers as r:1.
std::string disassemble(const uint8_t* actions, size_t size);
std::string disassemble(const std::vector<uint8_t>& actions);

// The name of an action code (0x96 "Push"), or "Unknown".
const char* actionName(uint8_t code);

} // namespace avm1
} // namespace as3
