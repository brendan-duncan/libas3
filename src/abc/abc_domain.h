#pragma once

// Running ABC (the abcFile of a SWF's DoABC tag) on the VM: its scripts,
// classes and methods, as a player runs them.
//
//   as3::VM vm;
//   as3::abc::Domain domain(vm);
//   std::string error;
//   if (!domain.load(bytes, error)) { /* not an abcFile */ }
//   as3::Val main = vm.findDefinition("game_fla.MainTimeline");
//
// A class defined in ABC is a class of the VM like any other: found by
// its qualified name (vm.findDefinition, findClass, and the scripts the VM
// compiles from source), made with new, extended by classes from source
// and extending native ones (a document class over the host's MovieClip),
// its methods called and overridden either way. A script's code runs the
// first time one of its definitions is needed, as with a DoABC tag's lazy
// flag; runEntry runs the last script of a file at once, as without it.

#include "abc_file.h"

#include "../runtime/value.h"

#include <memory>
#include <string>
#include <vector>

namespace as3 {

class VM;

namespace abc {

class Domain {
public:
    // Its definitions are found through vm from now on; the domain must
    // live while vm runs code of it (until vm.releaseObjects or the VM goes).
    explicit Domain(VM& vm);
    ~Domain();

    Domain(const Domain&) = delete;
    Domain& operator=(const Domain&) = delete;

    // Adds a file's definitions. False, with why, when the bytes are not an
    // abcFile (nothing is added then).
    bool load(const std::vector<uint8_t>& bytes, std::string& error);
    bool load(AbcFile file, std::string& error);

    // Runs the last script of the file loaded last, if not run yet: the
    // entry point of a DoABC tag without the lazy flag (and of a program
    // compiled from source with code outside its packages). Throws
    // ScriptException for an ActionScript error.
    void runEntry();
    // Runs each script of every file not run yet, in order.
    void runAll();

    // A definition by qualified name ("fl.controls.CheckBox", "Main"),
    // its script run first if need be; undefined when the domain has none.
    Val find(const std::string& qualifiedName);
    // Every definition the files have, qualified, in the order defined.
    std::vector<std::string> definitionNames() const;

    // The methods running, innermost first ("\tat fl.core.UIComponent/draw()[@12]"),
    // as they were when the last ActionScript error came out of one: what a
    // debugger player's getStackTrace would have said of them. Taken: the
    // next call gives the next error's.
    std::string takeErrorStack();

    // How many instructions have run, for tests and profiling.
    uint64_t instructionCount() const;

    struct State;

private:
    std::shared_ptr<State> _state;
};

} // namespace abc
} // namespace as3
