#include "js.h"

namespace as3 {
namespace js {

namespace {

// as3.js, as the build embeds it.
const unsigned char kRuntime[] = {
#include "as3_js.inc"
    0};

} // namespace

const std::string& runtime() {
    static const std::string text(reinterpret_cast<const char*>(kRuntime), sizeof kRuntime - 1);
    return text;
}

} // namespace js
} // namespace as3
