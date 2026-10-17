#pragma once

// What the compiler knows of the classes a player provides: their
// packages (so that a type named through a wildcard import, as Sprite with
// import flash.display.*, has its qualified name) and their superclasses
// (pushed as scopes when a class extending one is made, as compilers do).

#include <string>
#include <vector>

namespace as3 {
namespace abc {

struct PlayerClass {
    const char* package;
    const char* name;
    // Qualified with a dot; null for Object (or an interface).
    const char* superclass;
};

// The class, or null when it is not one the table has.
const PlayerClass* findPlayerClass(const std::string& package, const std::string& name);

// Whether a top-level name (package "") is the player's: a class such as
// int or Math, a function such as trace, or NaN, Infinity, undefined.
bool isTopLevelName(const std::string& name);

// Whether a package is one of the player's that the table has.
bool isPlayerPackage(const std::string& package);

} // namespace abc
} // namespace as3
