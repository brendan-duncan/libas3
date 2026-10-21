#include "avm1.h"
#include "avm1_actions.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace as3 {
namespace avm1 {

const char* actionName(uint8_t code) {
    switch (code) {
        case End: return "End";
        case NextFrame: return "NextFrame";
        case PrevFrame: return "PrevFrame";
        case Play: return "Play";
        case Stop: return "Stop";
        case ToggleQuality: return "ToggleQuality";
        case StopSounds: return "StopSounds";
        case Add: return "Add";
        case Subtract: return "Subtract";
        case Multiply: return "Multiply";
        case Divide: return "Divide";
        case Equals: return "Equals";
        case Less: return "Less";
        case And: return "And";
        case Or: return "Or";
        case Not: return "Not";
        case StringEquals: return "StringEquals";
        case StringLength: return "StringLength";
        case StringExtract: return "StringExtract";
        case Pop: return "Pop";
        case ToInteger: return "ToInteger";
        case GetVariable: return "GetVariable";
        case SetVariable: return "SetVariable";
        case SetTarget2: return "SetTarget2";
        case StringAdd: return "StringAdd";
        case GetProperty: return "GetProperty";
        case SetProperty: return "SetProperty";
        case CloneSprite: return "CloneSprite";
        case RemoveSprite: return "RemoveSprite";
        case Trace: return "Trace";
        case StartDrag: return "StartDrag";
        case EndDrag: return "EndDrag";
        case StringLess: return "StringLess";
        case Throw: return "Throw";
        case CastOp: return "CastOp";
        case ImplementsOp: return "ImplementsOp";
        case RandomNumber: return "RandomNumber";
        case MBStringLength: return "MBStringLength";
        case CharToAscii: return "CharToAscii";
        case AsciiToChar: return "AsciiToChar";
        case GetTime: return "GetTime";
        case MBStringExtract: return "MBStringExtract";
        case MBCharToAscii: return "MBCharToAscii";
        case MBAsciiToChar: return "MBAsciiToChar";
        case Delete: return "Delete";
        case Delete2: return "Delete2";
        case DefineLocal: return "DefineLocal";
        case CallFunction: return "CallFunction";
        case Return: return "Return";
        case Modulo: return "Modulo";
        case NewObject: return "NewObject";
        case DefineLocal2: return "DefineLocal2";
        case InitArray: return "InitArray";
        case InitObject: return "InitObject";
        case TypeOf: return "TypeOf";
        case TargetPath: return "TargetPath";
        case Enumerate: return "Enumerate";
        case Add2: return "Add2";
        case Less2: return "Less2";
        case Equals2: return "Equals2";
        case ToNumber: return "ToNumber";
        case ToString: return "ToString";
        case PushDuplicate: return "PushDuplicate";
        case StackSwap: return "StackSwap";
        case GetMember: return "GetMember";
        case SetMember: return "SetMember";
        case Increment: return "Increment";
        case Decrement: return "Decrement";
        case CallMethod: return "CallMethod";
        case NewMethod: return "NewMethod";
        case InstanceOf: return "InstanceOf";
        case Enumerate2: return "Enumerate2";
        case BitAnd: return "BitAnd";
        case BitOr: return "BitOr";
        case BitXor: return "BitXor";
        case BitLShift: return "BitLShift";
        case BitRShift: return "BitRShift";
        case BitURShift: return "BitURShift";
        case StrictEquals: return "StrictEquals";
        case Greater: return "Greater";
        case StringGreater: return "StringGreater";
        case Extends: return "Extends";
        case GotoFrame: return "GotoFrame";
        case GetURL: return "GetURL";
        case StoreRegister: return "StoreRegister";
        case ConstantPool: return "ConstantPool";
        case WaitForFrame: return "WaitForFrame";
        case SetTarget: return "SetTarget";
        case GotoLabel: return "GotoLabel";
        case WaitForFrame2: return "WaitForFrame2";
        case DefineFunction2: return "DefineFunction2";
        case Try: return "Try";
        case With: return "With";
        case Push: return "Push";
        case Jump: return "Jump";
        case GetURL2: return "GetURL2";
        case DefineFunction: return "DefineFunction";
        case If: return "If";
        case Call: return "Call";
        case GotoFrame2: return "GotoFrame2";
        default: return "Unknown";
    }
}

namespace {

// Reads a record's payload.
struct Reader {
    const uint8_t* data;
    size_t size;
    size_t at = 0;

    bool more() const { return at < size; }
    uint8_t u8() { return at < size ? data[at++] : 0; }
    uint16_t u16() {
        const uint16_t low = u8();
        return static_cast<uint16_t>(low | (u8() << 8));
    }
    uint32_t u32() {
        const uint32_t low = u16();
        return low | (static_cast<uint32_t>(u16()) << 16);
    }
    std::string string() {
        std::string out;
        while (at < size && data[at]) out += static_cast<char>(data[at++]);
        if (at < size) ++at;
        return out;
    }
};

std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

std::string numberText(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    std::ostringstream out;
    out.precision(17);
    out << value;
    std::string text = out.str();
    if (text.find_first_of(".e") == std::string::npos) text += ".0";
    return text;
}

class Disassembler {
public:
    Disassembler(const uint8_t* data, size_t size) : _data(data), _size(size) {}

    std::string run() {
        // First the jump targets, for labels; then the listing.
        scan();
        int next = 1;
        for (const auto& target : _sources) _labels[target.first] = "L" + std::to_string(next++);
        _waiting = _sources;
        std::string out;
        size_t at = 0;
        while (at < _size) {
            closeBlocks(at, out);
            printLabel(at, out);
            const uint8_t code = _data[at];
            size_t length = 0;
            const size_t start = at + 1 + (code >= 0x80 ? 2 : 0);
            if (code >= 0x80) {
                if (at + 3 > _size) {
                    out += indent(depth()) + "(cut short)\n";
                    break;
                }
                length = static_cast<size_t>(_data[at + 1] | (_data[at + 2] << 8));
            }
            if (start + length > _size) {
                out += indent(depth()) + "(cut short)\n";
                break;
            }
            Reader payload{_data + start, length};
            const size_t after = start + length;
            std::string line = actionName(code);
            switch (code) {
                case ConstantPool: {
                    _pool.clear();
                    const uint16_t count = payload.u16();
                    for (uint16_t i = 0; i < count; ++i) {
                        _pool.push_back(payload.string());
                        line += " " + quoted(_pool.back());
                    }
                    break;
                }
                case Push:
                    while (payload.more()) line += " " + pushValue(payload);
                    break;
                case Jump:
                case If: {
                    const int16_t offset = static_cast<int16_t>(payload.u16());
                    const size_t target = static_cast<size_t>(static_cast<long>(after) + offset);
                    const auto found = _labels.find(target);
                    line += " " + (found != _labels.end() ? found->second : "?" + std::to_string(offset));
                    break;
                }
                case GotoFrame:
                    line += " " + std::to_string(payload.u16());
                    break;
                case GetURL: {
                    const std::string url = payload.string();
                    line += " " + quoted(url) + " " + quoted(payload.string());
                    break;
                }
                case StoreRegister:
                    line += " r:" + std::to_string(payload.u8());
                    break;
                case WaitForFrame: {
                    const uint16_t frame = payload.u16();
                    line += " " + std::to_string(frame) + " " + std::to_string(payload.u8());
                    break;
                }
                case SetTarget:
                case GotoLabel:
                    line += " " + quoted(payload.string());
                    break;
                case WaitForFrame2:
                    line += " " + std::to_string(payload.u8());
                    break;
                case GetURL2: {
                    const uint8_t flags = payload.u8();
                    const int method = flags & 0x03;
                    if (method) line += method == 1 ? " GET" : method == 2 ? " POST" : " ?";
                    if (flags & 0x40) line += " target";
                    if (flags & 0x80) line += " variables";
                    break;
                }
                case GotoFrame2: {
                    const uint8_t flags = payload.u8();
                    if (flags & 0x01) line += " play";
                    if (flags & 0x02) line += " bias " + std::to_string(payload.u16());
                    break;
                }
                case With: {
                    const uint16_t size = payload.u16();
                    out += indent(depth()) + line + " {\n";
                    _ends.push_back({after, after + size, "}"});
                    at = after;
                    continue;
                }
                case DefineFunction:
                case DefineFunction2: {
                    const std::string name = payload.string();
                    const uint16_t count = payload.u16();
                    line += " " + quoted(name);
                    std::string params;
                    if (code == DefineFunction2) {
                        const uint8_t registers = payload.u8();
                        const uint16_t flags = payload.u16();
                        for (uint16_t i = 0; i < count; ++i) {
                            const uint8_t r = payload.u8();
                            const std::string param = payload.string();
                            params += (i ? ", " : "") + param + (r ? "=r:" + std::to_string(r) : std::string());
                        }
                        line += " (" + params + ") registers " + std::to_string(registers);
                        static const std::pair<uint16_t, const char*> names[] = {
                            {PreloadThis, "this"},           {PreloadArguments, "arguments"},
                            {PreloadSuper, "super"},         {PreloadRoot, "_root"},
                            {PreloadParent, "_parent"},      {PreloadGlobal, "_global"},
                        };
                        std::string preloads;
                        for (const auto& [flag, word] : names) {
                            if (flags & flag) preloads += preloads.empty() ? word : std::string(" ") + word;
                        }
                        if (!preloads.empty()) line += " preload " + preloads;
                    } else {
                        for (uint16_t i = 0; i < count; ++i) params += (i ? ", " : "") + payload.string();
                        line += " (" + params + ")";
                    }
                    const uint16_t size = payload.u16();
                    out += indent(depth()) + line + " {\n";
                    _ends.push_back({after, after + size, "}"});
                    at = after;
                    continue;
                }
                case Try: {
                    const uint8_t flags = payload.u8();
                    const uint16_t trySize = payload.u16();
                    const uint16_t catchSize = payload.u16();
                    const uint16_t finallySize = payload.u16();
                    if (flags & 0x04) {
                        line += " r:" + std::to_string(payload.u8());
                    } else {
                        line += " " + quoted(payload.string());
                    }
                    out += indent(depth()) + line + " {\n";
                    // Closed in reverse: the finally block's end, the catch
                    // block's, the try block's.
                    const size_t tryEnd = after + trySize;
                    const size_t catchEnd = tryEnd + catchSize;
                    const size_t finallyEnd = catchEnd + finallySize;
                    if (flags & 0x02) _ends.push_back({catchEnd, finallyEnd, "}"});
                    if (flags & 0x01) _ends.push_back({tryEnd, catchEnd, flags & 0x02 ? "} finally {" : "}"});
                    _ends.push_back({after, tryEnd, (flags & 0x01) ? "} catch {" : (flags & 0x02) ? "} finally {" : "}"});
                    at = after;
                    continue;
                }
                default:
                    if (code >= 0x80 && length) line += " (" + std::to_string(length) + " bytes)";
                    break;
            }
            out += indent(depth()) + line + "\n";
            at = after;
        }
        closeBlocks(_size, out);
        printLabel(_size, out);
        closeBlocks(static_cast<size_t>(-1), out);
        return out;
    }

private:
    const uint8_t* _data;
    size_t _size;
    // Each jump target and where the jumps to it are; those not yet shown
    // by a label.
    std::map<size_t, std::vector<size_t>> _sources;
    std::map<size_t, std::vector<size_t>> _waiting;
    std::map<size_t, std::string> _labels;
    std::vector<std::string> _pool;

    // Where the blocks open end, innermost last, and what closes each: a
    // "}" closes one; "} catch {" goes on to the next part of the same.
    struct Open {
        size_t start;
        size_t end;
        std::string closer;
    };
    std::vector<Open> _ends;

    static std::string indent(size_t depth) { return std::string(depth * 4, ' '); }

    // The label at \a at, for the jumps to it from between \a from and
    // \a to that it has not been shown for.
    void printLabel(size_t at, std::string& out, size_t from = 0, size_t to = static_cast<size_t>(-1)) {
        const auto waiting = _waiting.find(at);
        if (waiting == _waiting.end()) return;
        std::vector<size_t>& jumps = waiting->second;
        const auto inside = [&](size_t source) { return source >= from && source < to; };
        if (std::none_of(jumps.begin(), jumps.end(), inside)) return;
        jumps.erase(std::remove_if(jumps.begin(), jumps.end(), inside), jumps.end());
        out += _labels[at] + ":\n";
    }

    size_t depth() const {
        size_t count = 0;
        for (const auto& end : _ends) count += end.closer == "}" ? 1 : 0;
        return count;
    }

    void closeBlocks(size_t at, std::string& out) {
        while (!_ends.empty() && at >= _ends.back().end) {
            // A jump from inside to the block's end lands on its last line.
            const Open block = _ends.back();
            printLabel(at, out, block.start, block.end);
            const std::string closer = block.closer;
            _ends.pop_back();
            const size_t now = depth();
            out += indent(closer == "}" ? now : now - 1) + closer + "\n";
        }
    }

    void scan() {
        size_t at = 0;
        while (at < _size) {
            const uint8_t code = _data[at];
            size_t length = 0;
            if (code >= 0x80) {
                if (at + 3 > _size) return;
                length = static_cast<size_t>(_data[at + 1] | (_data[at + 2] << 8));
            }
            const size_t after = at + 1 + (code >= 0x80 ? 2 : 0) + length;
            if ((code == Jump || code == If) && length >= 2) {
                const int16_t offset = static_cast<int16_t>(_data[at + 3] | (_data[at + 4] << 8));
                _sources[static_cast<size_t>(static_cast<long>(after) + offset)].push_back(at);
            }
            at = after;
        }
    }

    std::string pushValue(Reader& payload) {
        const uint8_t type = payload.u8();
        switch (type) {
            case PushString: return quoted(payload.string());
            case PushFloat: {
                const uint32_t bits = payload.u32();
                float value = 0;
                std::memcpy(&value, &bits, sizeof(value));
                return numberText(value);
            }
            case PushNull: return "null";
            case PushUndefined: return "undefined";
            case PushRegister: return "r:" + std::to_string(payload.u8());
            case PushBoolean: return payload.u8() ? "true" : "false";
            case PushDouble: {
                const uint64_t high = payload.u32();
                const uint64_t low = payload.u32();
                const uint64_t bits = (high << 32) | low;
                double value = 0;
                std::memcpy(&value, &bits, sizeof(value));
                return numberText(value);
            }
            case PushInteger: return std::to_string(static_cast<int32_t>(payload.u32()));
            case PushConstant8:
            case PushConstant16: {
                const uint16_t index = type == PushConstant8 ? payload.u8() : payload.u16();
                return index < _pool.size() ? quoted(_pool[index]) : "pool:" + std::to_string(index);
            }
            default: return "?";
        }
    }
};

} // namespace

std::string disassemble(const uint8_t* actions, size_t size) {
    return Disassembler(actions, size).run();
}

std::string disassemble(const std::vector<uint8_t>& actions) {
    return disassemble(actions.data(), actions.size());
}

} // namespace avm1
} // namespace as3
