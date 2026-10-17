#pragma once

// Checks an abcFile as a player's verifier would before running it (the
// structure, not the types): every pool index in range, every method that
// runs with one body and interface methods without, and in each body that
// instructions decode, branches land on instructions, and that along every
// path the operand stack never goes below empty or above max_stack and the
// scope stack stays within its depths and agrees where paths meet. Also
// prints a file as text.

#include "abc_file.h"

#include <string>
#include <vector>

namespace as3 {
namespace abc {

// What is wrong, a line each ("method 12 (Main/draw): ..."); empty when
// nothing is.
std::vector<std::string> verify(const AbcFile& file);

// The whole file as text: its classes, traits and each method's code.
std::string disassemble(const AbcFile& file);

// One body's code, an instruction a line, with offsets.
std::string disassembleBody(const AbcFile& file, const MethodBody& body);

// What a method is called where it is used ("pkg.Class/name", "pkg.Class
// iinit", "script 0 init"), for messages.
std::vector<std::string> methodNames(const AbcFile& file);

} // namespace abc
} // namespace as3
