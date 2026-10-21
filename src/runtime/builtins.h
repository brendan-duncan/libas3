#pragma once

// The built-in library: the top-level classes and functions ActionScript
// has (Object, Array, String, Math, Date, RegExp, JSON, Error...) and
// those of flash.utils (Dictionary, getTimer, setTimeout...).

#include "../bytecode/vm.h"
#include <string>

namespace as3 {

void defineCoreBuiltins(VM& vm);
void defineArrayBuiltins(VM& vm);
void defineStringBuiltins(VM& vm);
void defineDateBuiltins(VM& vm);
void defineJsonBuiltins(VM& vm);
void defineUtilsBuiltins(VM& vm);
void defineXmlBuiltins(VM& vm);
// Namespace and QName (builtins_namespace.cpp); a Namespace of a URI, as
// a namespace constant is.
void defineNamespaceBuiltins(VM& vm);
Val makeNamespaceValue(VM& vm, const std::string& uri);

// Strings are kept as UTF-8; ActionScript counts in UTF-16 units.
std::u16string toUtf16(const std::string& text);
std::string fromUtf16(const std::u16string& text);
bool isAscii(const std::string& text);

// The comparison Array.sort and Vector.sort make, with sort's options.
namespace ArraySort {
constexpr int CASEINSENSITIVE = 1;
constexpr int DESCENDING = 2;
constexpr int UNIQUESORT = 4;
constexpr int RETURNINDEXEDARRAY = 8;
constexpr int NUMERIC = 16;
}

// A native method's 'this' as its host state, or a TypeError.
template <class T>
T& hostOf(VM& vm, const Val& self, const char* className) {
    T* state = self.host<T>();
    if (!state) {
        vm.throwError("TypeError", std::string("Error #1034: Type Coercion failed: ") + className + " expected.", 1034);
    }
    return *state;
}

} // namespace as3
