#pragma once

// Zones for the Tracy profiler (https://github.com/wolfpld/tracy) on what
// a host asks of libas3: parsing, compiling, running a script, calling one,
// running timers, AVM1 actions and ABC. Built with the CMake option
// AS3_TRACY; without it they are nothing.

#ifdef AS3_TRACY
#include <tracy/Tracy.hpp>
// A zone for the rest of the scope, named by a string literal.
#define AS3_ZONE(name) ZoneScopedN(name)
// Text shown with the zone (what was run).
#define AS3_ZONE_TEXT(text) \
    do { \
        const std::string& zoneText_ = (text); \
        ZoneText(zoneText_.data(), zoneText_.size()); \
    } while (0)
#else
#define AS3_ZONE(name) \
    do { \
    } while (0)
#define AS3_ZONE_TEXT(text) \
    do { \
    } while (0)
#endif
