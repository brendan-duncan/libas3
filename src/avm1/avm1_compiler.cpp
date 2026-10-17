#include "avm1.h"
#include "avm1_actions.h"

#include "../parser/parser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <set>
#include <sstream>

namespace as3 {
namespace avm1 {

namespace {

// Thrown to give up on a script; the message is already among the errors.
struct Abandon {};

// A value to push.
struct Value {
    PushType type = PushUndefined;
    std::string text;
    double number = 0;
    int32_t integer = 0;
    bool boolean = false;
    uint8_t reg = 0;

    static Value string(const std::string& s) { Value v; v.type = PushString; v.text = s; return v; }
    static Value num(double d) {
        Value v;
        if (std::isfinite(d) && d == std::floor(d) && d >= -2147483648.0 && d <= 2147483647.0 &&
            !(d == 0 && std::signbit(d))) {
            v.type = PushInteger;
            v.integer = static_cast<int32_t>(d);
        } else {
            v.type = PushDouble;
            v.number = d;
        }
        return v;
    }
    static Value boolean_(bool b) { Value v; v.type = PushBoolean; v.boolean = b; return v; }
    static Value null() { Value v; v.type = PushNull; return v; }
    static Value undefined() { Value v; v.type = PushUndefined; return v; }
    static Value registerValue(uint8_t r) { Value v; v.type = PushRegister; v.reg = r; return v; }
};

// The strings a script's ConstantPool holds, in the order first used.
class Pool {
public:
    // The entry for \a text, or -1 once the pool is full.
    int indexOf(const std::string& text) {
        const auto found = _index.find(text);
        if (found != _index.end()) {
            return found->second;
        }
        // The pool's record has a 16-bit length, its count included.
        if (_entries.size() >= 65535 || _bytes + text.size() + 1 > 60000 ||
            text.find('\0') != std::string::npos) {
            return -1;
        }
        const int index = static_cast<int>(_entries.size());
        _entries.push_back(text);
        _index[text] = index;
        _bytes += text.size() + 1;
        return index;
    }
    bool empty() const { return _entries.empty(); }
    const std::vector<std::string>& entries() const { return _entries; }

private:
    std::vector<std::string> _entries;
    std::map<std::string, int> _index;
    size_t _bytes = 2;
};

// A place in the code jumps go to.
struct Label {
    long position = -1;
    std::vector<size_t> fixups;
};

// Action records, written one after another. Pushes next to each other go
// in one Push record, unless a jump lands between them.
class Code {
public:
    explicit Code(Pool& pool) : _pool(pool) {}

    std::vector<uint8_t> bytes;

    size_t size() const { return bytes.size(); }

    void action(uint8_t code) {
        closePush();
        bytes.push_back(code);
    }

    // A record with a payload; where the payload starts.
    size_t record(uint8_t code, const std::vector<uint8_t>& payload) {
        closePush();
        bytes.push_back(code);
        u16(static_cast<uint16_t>(payload.size()));
        const size_t start = bytes.size();
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return start;
    }

    void push(const Value& value) {
        std::vector<uint8_t> encoded;
        encode(value, encoded);
        if (_open == npos || bytes.size() - _open + encoded.size() > 65000) {
            closePush();
            _open = bytes.size();
            bytes.push_back(Action::Push);
            bytes.push_back(0);
            bytes.push_back(0);
        }
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
        const size_t length = bytes.size() - (_open + 3);
        bytes[_open + 1] = static_cast<uint8_t>(length & 0xFF);
        bytes[_open + 2] = static_cast<uint8_t>(length >> 8);
    }
    void pushString(const std::string& s) { push(Value::string(s)); }
    void pushNumber(double d) { push(Value::num(d)); }
    void pushRegister(uint8_t r) { push(Value::registerValue(r)); }

    void storeRegister(uint8_t r) { record(Action::StoreRegister, {r}); }

    // A Jump or If to \a label.
    void jump(uint8_t code, Label& label) {
        const size_t at = record(code, {0, 0});
        if (label.position >= 0) {
            patch(at, static_cast<size_t>(label.position));
        } else {
            label.fixups.push_back(at);
        }
    }

    void bind(Label& label) {
        closePush();
        label.position = static_cast<long>(bytes.size());
        for (size_t at : label.fixups) {
            patch(at, bytes.size());
        }
        label.fixups.clear();
    }

    // Writes a 16-bit value at \a at.
    void put16(size_t at, size_t value) {
        bytes[at] = static_cast<uint8_t>(value & 0xFF);
        bytes[at + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    }

    void closePush() { _open = npos; }

    // Whether a jump went further than a 16-bit offset reaches.
    bool overflowed = false;

private:
    static constexpr size_t npos = static_cast<size_t>(-1);
    Pool& _pool;
    size_t _open = npos;

    void u16(uint16_t value) {
        bytes.push_back(static_cast<uint8_t>(value & 0xFF));
        bytes.push_back(static_cast<uint8_t>(value >> 8));
    }

    void patch(size_t at, size_t target) {
        const long offset = static_cast<long>(target) - static_cast<long>(at + 2);
        if (offset < -32768 || offset > 32767) {
            overflowed = true;
        }
        put16(at, static_cast<size_t>(offset) & 0xFFFF);
    }

    void encode(const Value& value, std::vector<uint8_t>& out) {
        switch (value.type) {
            case PushString: {
                const int index = _pool.indexOf(value.text);
                if (index >= 0 && index < 256) {
                    out.push_back(PushConstant8);
                    out.push_back(static_cast<uint8_t>(index));
                } else if (index >= 256) {
                    out.push_back(PushConstant16);
                    out.push_back(static_cast<uint8_t>(index & 0xFF));
                    out.push_back(static_cast<uint8_t>(index >> 8));
                } else {
                    out.push_back(PushString);
                    out.insert(out.end(), value.text.begin(), value.text.end());
                    out.push_back(0);
                }
                break;
            }
            case PushNull:
            case PushUndefined:
                out.push_back(value.type);
                break;
            case PushRegister:
                out.push_back(PushRegister);
                out.push_back(value.reg);
                break;
            case PushBoolean:
                out.push_back(PushBoolean);
                out.push_back(value.boolean ? 1 : 0);
                break;
            case PushInteger: {
                out.push_back(PushInteger);
                const uint32_t bits = static_cast<uint32_t>(value.integer);
                for (int i = 0; i < 4; ++i) {
                    out.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFF));
                }
                break;
            }
            default: {
                // A double's two 32-bit halves, the high one first, each
                // little-endian.
                out.push_back(PushDouble);
                uint64_t bits = 0;
                std::memcpy(&bits, &value.number, sizeof(bits));
                const uint32_t high = static_cast<uint32_t>(bits >> 32);
                const uint32_t low = static_cast<uint32_t>(bits & 0xFFFFFFFFu);
                for (int i = 0; i < 4; ++i) {
                    out.push_back(static_cast<uint8_t>((high >> (8 * i)) & 0xFF));
                }
                for (int i = 0; i < 4; ++i) {
                    out.push_back(static_cast<uint8_t>((low >> (8 * i)) & 0xFF));
                }
                break;
            }
        }
    }
};

void appendString(std::vector<uint8_t>& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
    out.push_back(0);
}

void appendU16(std::vector<uint8_t>& out, size_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

// The movie clip properties getProperty and setProperty name by number.
int propertyIndex(const std::string& name) {
    static const char* names[] = {"_x", "_y", "_xscale", "_yscale", "_currentframe", "_totalframes", "_alpha",
                                  "_visible", "_width", "_height", "_rotation", "_target", "_framesloaded", "_name",
                                  "_droptarget", "_url", "_highquality", "_focusrect", "_soundbuftime", "_quality",
                                  "_xmouse", "_ymouse"};
    for (int i = 0; i < static_cast<int>(sizeof(names) / sizeof(names[0])); ++i) {
        if (name == names[i]) {
            return i;
        }
    }
    return -1;
}

// A way out of a try block with a finally block: a break, a continue or a
// return, taken once the finally block has run.
struct TryExit {
    enum Kind { Break, Continue, Return } kind = Break;
    std::string label;
    int line = 0;
};

// What a break or continue may leave: a loop, a switch, or a labeled
// statement; a for-in loop has its names on the stack until it ends. A try
// block with a finally block (or its catch block) is left by running to its
// end, the way out kept in a register, so the finally block runs first.
struct Breakable {
    std::vector<std::string> labels;
    Label* breakTo = nullptr;
    Label* continueTo = nullptr;
    bool forIn = false;
    bool loop = false;
    bool isSwitch = false;
    bool finally = false;
    Label* blockEnd = nullptr;
    uint8_t exitRegister = 0;
    uint8_t valueRegister = 0;
    std::vector<TryExit>* exits = nullptr;
};

// The function being compiled, or the script's top level.
struct Scope {
    bool function = false;
    // Locals kept in registers, by name.
    std::map<std::string, uint8_t> registers;
    uint8_t thisRegister = 0;
    uint8_t argumentsRegister = 0;
    uint8_t superRegister = 0;
    // The next register free, and the most used.
    int nextRegister = 1;
    int highest = 0;
    std::vector<uint8_t> freeTemps;
    std::vector<Breakable> breakables;
    // At the top level, where a return goes.
    Label* exit = nullptr;
    // The function this one is in, and the names this one declares
    // (parameters, variables, functions, caught errors): they come before a
    // class's members.
    Scope* outer = nullptr;
    std::set<std::string> declared;
};

// What the parts of a function use, for the registers it gets.
struct Usage {
    bool usesThis = false;
    bool usesArguments = false;
    bool usesSuper = false;
    // with or eval: names looked up by string, so locals cannot be in
    // registers.
    bool dynamic = false;
    std::set<std::string> locals;
    // The names nested functions use: what they find in this function's
    // scope, so not in registers.
    std::set<std::string> captured;
    std::set<std::string> declaredFunctions;
    std::set<std::string> catches;
    // In a class's method: whether a bare name is one of the class's members,
    // found on this.
    const std::function<bool(const std::string&)>* member = nullptr;
};

// The names an expression or a statement mentions, nested functions and all.
void namesIn(AstNode* node, std::set<std::string>& names);

void namesInExpression(Expression* expr, std::set<std::string>& names) {
    if (!expr) {
        return;
    }
    namesIn(expr, names);
}

void namesIn(AstNode* node, std::set<std::string>& names) {
    if (!node) {
        return;
    }
    if (auto* id = dynamic_cast<Identifier*>(node)) {
        names.insert(id->name);
    } else if (auto* b = dynamic_cast<BinaryExpression*>(node)) {
        namesIn(b->left.get(), names);
        namesIn(b->right.get(), names);
    } else if (auto* u = dynamic_cast<UnaryExpression*>(node)) {
        namesIn(u->operand.get(), names);
    } else if (auto* c = dynamic_cast<ConditionalExpression*>(node)) {
        namesIn(c->test.get(), names);
        namesIn(c->consequent.get(), names);
        namesIn(c->alternate.get(), names);
    } else if (auto* a = dynamic_cast<AssignmentExpression*>(node)) {
        namesIn(a->left.get(), names);
        namesIn(a->right.get(), names);
    } else if (auto* call = dynamic_cast<CallExpression*>(node)) {
        namesIn(call->callee.get(), names);
        for (auto& arg : call->arguments) namesIn(arg.get(), names);
    } else if (auto* n = dynamic_cast<NewExpression*>(node)) {
        namesIn(n->callee.get(), names);
        for (auto& arg : n->arguments) namesIn(arg.get(), names);
    } else if (auto* m = dynamic_cast<MemberExpression*>(node)) {
        namesIn(m->object.get(), names);
        if (m->computed) namesIn(m->computedProperty.get(), names);
    } else if (auto* f = dynamic_cast<FunctionExpression*>(node)) {
        for (auto& p : f->parameters) {
            if (p->name) names.insert(p->name->name);
            namesIn(p->defaultValue.get(), names);
        }
        namesIn(f->body.get(), names);
    } else if (auto* arr = dynamic_cast<ArrayExpression*>(node)) {
        for (auto& e : arr->elements) namesIn(e.get(), names);
    } else if (auto* obj = dynamic_cast<ObjectExpression*>(node)) {
        for (auto& p : obj->properties) namesIn(p.second.get(), names);
    } else if (auto* seq = dynamic_cast<SequenceExpression*>(node)) {
        for (auto& e : seq->expressions) namesIn(e.get(), names);
    } else if (auto* block = dynamic_cast<BlockStatement*>(node)) {
        for (auto& s : block->statements) namesIn(s.get(), names);
    } else if (auto* es = dynamic_cast<ExpressionStatement*>(node)) {
        namesIn(es->expression.get(), names);
    } else if (auto* is = dynamic_cast<IfStatement*>(node)) {
        namesIn(is->test.get(), names);
        namesIn(is->consequent.get(), names);
        namesIn(is->alternate.get(), names);
    } else if (auto* ws = dynamic_cast<WhileStatement*>(node)) {
        namesIn(ws->test.get(), names);
        namesIn(ws->body.get(), names);
    } else if (auto* ds = dynamic_cast<DoWhileStatement*>(node)) {
        namesIn(ds->body.get(), names);
        namesIn(ds->test.get(), names);
    } else if (auto* fs = dynamic_cast<ForStatement*>(node)) {
        namesIn(fs->init.get(), names);
        namesIn(fs->test.get(), names);
        namesIn(fs->update.get(), names);
        namesIn(fs->body.get(), names);
    } else if (auto* fi = dynamic_cast<ForInStatement*>(node)) {
        namesIn(fi->left.get(), names);
        namesIn(fi->right.get(), names);
        namesIn(fi->body.get(), names);
    } else if (auto* sw = dynamic_cast<SwitchStatement*>(node)) {
        namesIn(sw->discriminant.get(), names);
        for (auto& c : sw->cases) {
            namesIn(c->test.get(), names);
            for (auto& s : c->consequent) namesIn(s.get(), names);
        }
    } else if (auto* rs = dynamic_cast<ReturnStatement*>(node)) {
        namesIn(rs->argument.get(), names);
    } else if (auto* ts = dynamic_cast<ThrowStatement*>(node)) {
        namesIn(ts->argument.get(), names);
    } else if (auto* tr = dynamic_cast<TryStatement*>(node)) {
        namesIn(tr->block.get(), names);
        for (auto& c : tr->catches) {
            if (c.variable) names.insert(c.variable->name);
            namesIn(c.body.get(), names);
        }
        namesIn(tr->finallyBody.get(), names);
    } else if (auto* w = dynamic_cast<WithStatement*>(node)) {
        namesIn(w->object.get(), names);
        namesIn(w->body.get(), names);
    } else if (auto* ls = dynamic_cast<LabeledStatement*>(node)) {
        namesIn(ls->body.get(), names);
    } else if (auto* vd = dynamic_cast<VariableDefinition*>(node)) {
        for (auto& d : vd->declarations) {
            if (d->id) names.insert(d->id->name);
            namesIn(d->init.get(), names);
        }
    } else if (auto* fd = dynamic_cast<FunctionDefinition*>(node)) {
        if (fd->name) names.insert(fd->name->name);
        for (auto& p : fd->parameters) {
            if (p->name) names.insert(p->name->name);
            namesIn(p->defaultValue.get(), names);
        }
        namesIn(fd->body.get(), names);
    }
}

// Whether statement \a node has a break, continue or return in it (not in
// the functions it defines): what may jump out of a try block.
bool jumpsIn(AstNode* node) {
    if (!node) return false;
    if (dynamic_cast<BreakStatement*>(node) || dynamic_cast<ContinueStatement*>(node) ||
        dynamic_cast<ReturnStatement*>(node)) {
        return true;
    }
    if (auto* block = dynamic_cast<BlockStatement*>(node)) {
        for (auto& s : block->statements) {
            if (jumpsIn(s.get())) return true;
        }
    } else if (auto* is = dynamic_cast<IfStatement*>(node)) {
        return jumpsIn(is->consequent.get()) || jumpsIn(is->alternate.get());
    } else if (auto* ws = dynamic_cast<WhileStatement*>(node)) {
        return jumpsIn(ws->body.get());
    } else if (auto* ds = dynamic_cast<DoWhileStatement*>(node)) {
        return jumpsIn(ds->body.get());
    } else if (auto* fs = dynamic_cast<ForStatement*>(node)) {
        return jumpsIn(fs->body.get());
    } else if (auto* fi = dynamic_cast<ForInStatement*>(node)) {
        return jumpsIn(fi->body.get());
    } else if (auto* sw = dynamic_cast<SwitchStatement*>(node)) {
        for (auto& c : sw->cases) {
            for (auto& s : c->consequent) {
                if (jumpsIn(s.get())) return true;
            }
        }
    } else if (auto* tr = dynamic_cast<TryStatement*>(node)) {
        if (jumpsIn(tr->block.get()) || jumpsIn(tr->finallyBody.get())) return true;
        for (auto& c : tr->catches) {
            if (jumpsIn(c.body.get())) return true;
        }
    } else if (auto* w = dynamic_cast<WithStatement*>(node)) {
        return jumpsIn(w->body.get());
    } else if (auto* ls = dynamic_cast<LabeledStatement*>(node)) {
        return jumpsIn(ls->body.get());
    }
    return false;
}

// What a function's own code (not the functions in it) uses.
void usageIn(AstNode* node, Usage& usage) {
    if (!node) {
        return;
    }
    if (auto* id = dynamic_cast<Identifier*>(node)) {
        if (id->name == "arguments") usage.usesArguments = true;
        if (usage.member && (*usage.member)(id->name)) usage.usesThis = true;
        return;
    }
    if (dynamic_cast<ThisExpression*>(node)) {
        usage.usesThis = true;
        return;
    }
    if (dynamic_cast<SuperExpression*>(node)) {
        usage.usesSuper = true;
        return;
    }
    if (auto* f = dynamic_cast<FunctionExpression*>(node)) {
        namesIn(f, usage.captured);
        return;
    }
    if (auto* fd = dynamic_cast<FunctionDefinition*>(node)) {
        if (fd->name) usage.declaredFunctions.insert(fd->name->name);
        for (auto& p : fd->parameters) {
            namesIn(p->defaultValue.get(), usage.captured);
        }
        namesIn(fd->body.get(), usage.captured);
        return;
    }
    if (auto* b = dynamic_cast<BinaryExpression*>(node)) {
        usageIn(b->left.get(), usage);
        usageIn(b->right.get(), usage);
    } else if (auto* u = dynamic_cast<UnaryExpression*>(node)) {
        usageIn(u->operand.get(), usage);
    } else if (auto* c = dynamic_cast<ConditionalExpression*>(node)) {
        usageIn(c->test.get(), usage);
        usageIn(c->consequent.get(), usage);
        usageIn(c->alternate.get(), usage);
    } else if (auto* a = dynamic_cast<AssignmentExpression*>(node)) {
        usageIn(a->left.get(), usage);
        usageIn(a->right.get(), usage);
    } else if (auto* call = dynamic_cast<CallExpression*>(node)) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (callee->name == "eval") usage.dynamic = true;
            // tellTarget's block runs where the target is.
            if (callee->name == "__tellTarget") usage.dynamic = true;
        }
        usageIn(call->callee.get(), usage);
        for (auto& arg : call->arguments) {
            // tellTarget's block is code of this function, not one of its
            // own.
            auto* block = dynamic_cast<FunctionExpression*>(arg.get());
            auto* callee = dynamic_cast<Identifier*>(call->callee.get());
            if (block && callee && callee->name == "__tellTarget") {
                usageIn(block->body.get(), usage);
            } else {
                usageIn(arg.get(), usage);
            }
        }
    } else if (auto* n = dynamic_cast<NewExpression*>(node)) {
        usageIn(n->callee.get(), usage);
        for (auto& arg : n->arguments) usageIn(arg.get(), usage);
    } else if (auto* m = dynamic_cast<MemberExpression*>(node)) {
        usageIn(m->object.get(), usage);
        if (m->computed) usageIn(m->computedProperty.get(), usage);
    } else if (auto* arr = dynamic_cast<ArrayExpression*>(node)) {
        for (auto& e : arr->elements) usageIn(e.get(), usage);
    } else if (auto* obj = dynamic_cast<ObjectExpression*>(node)) {
        for (auto& p : obj->properties) usageIn(p.second.get(), usage);
    } else if (auto* seq = dynamic_cast<SequenceExpression*>(node)) {
        for (auto& e : seq->expressions) usageIn(e.get(), usage);
    } else if (auto* block = dynamic_cast<BlockStatement*>(node)) {
        for (auto& s : block->statements) usageIn(s.get(), usage);
    } else if (auto* es = dynamic_cast<ExpressionStatement*>(node)) {
        usageIn(es->expression.get(), usage);
    } else if (auto* is = dynamic_cast<IfStatement*>(node)) {
        usageIn(is->test.get(), usage);
        usageIn(is->consequent.get(), usage);
        usageIn(is->alternate.get(), usage);
    } else if (auto* ws = dynamic_cast<WhileStatement*>(node)) {
        usageIn(ws->test.get(), usage);
        usageIn(ws->body.get(), usage);
    } else if (auto* ds = dynamic_cast<DoWhileStatement*>(node)) {
        usageIn(ds->body.get(), usage);
        usageIn(ds->test.get(), usage);
    } else if (auto* fs = dynamic_cast<ForStatement*>(node)) {
        usageIn(fs->init.get(), usage);
        usageIn(fs->test.get(), usage);
        usageIn(fs->update.get(), usage);
        usageIn(fs->body.get(), usage);
    } else if (auto* fi = dynamic_cast<ForInStatement*>(node)) {
        usageIn(fi->left.get(), usage);
        usageIn(fi->right.get(), usage);
        usageIn(fi->body.get(), usage);
    } else if (auto* sw = dynamic_cast<SwitchStatement*>(node)) {
        usageIn(sw->discriminant.get(), usage);
        for (auto& c : sw->cases) {
            usageIn(c->test.get(), usage);
            for (auto& s : c->consequent) usageIn(s.get(), usage);
        }
    } else if (auto* rs = dynamic_cast<ReturnStatement*>(node)) {
        usageIn(rs->argument.get(), usage);
    } else if (auto* ts = dynamic_cast<ThrowStatement*>(node)) {
        usageIn(ts->argument.get(), usage);
    } else if (auto* tr = dynamic_cast<TryStatement*>(node)) {
        usageIn(tr->block.get(), usage);
        for (auto& c : tr->catches) {
            // A caught error is a name in the scope, never a register.
            if (c.variable) {
                usage.captured.insert(c.variable->name);
                usage.catches.insert(c.variable->name);
            }
            usageIn(c.body.get(), usage);
        }
        usageIn(tr->finallyBody.get(), usage);
    } else if (auto* w = dynamic_cast<WithStatement*>(node)) {
        usage.dynamic = true;
        usageIn(w->object.get(), usage);
        usageIn(w->body.get(), usage);
    } else if (auto* ls = dynamic_cast<LabeledStatement*>(node)) {
        usageIn(ls->body.get(), usage);
    } else if (auto* vd = dynamic_cast<VariableDefinition*>(node)) {
        for (auto& d : vd->declarations) {
            if (d->id) usage.locals.insert(d->id->name);
            usageIn(d->init.get(), usage);
        }
    }
}

// A function as a class or a script defines it: its name (empty for an
// expression), parameters and body.
struct FunctionParts {
    std::string name;
    const std::vector<std::unique_ptr<Parameter>>* parameters = nullptr;
    Statement* body = nullptr;
    // A class's constructor that extends another and does not call super()
    // itself calls it first.
    bool callSuper = false;
    int line = 0;
};

// The members a class declares.
ClassMembers membersOf(ClassDefinition* definition) {
    ClassMembers out;
    if (definition->superClass) out.superName = definition->superClass->name;
    const std::string& full = definition->name ? definition->name->name : std::string();
    const std::string shortName = full.substr(full.find_last_of('.') == std::string::npos ? 0 : full.find_last_of('.') + 1);
    for (auto& member : definition->members) {
        if (auto* fd = dynamic_cast<FunctionDefinition*>(member.get())) {
            if (!fd->name || fd->name->name == shortName) continue;
            (hasAttribute(fd->attributes, AttributeType::Static) ? out.statics : out.instance).push_back(fd->name->name);
        } else if (auto* vd = dynamic_cast<VariableDefinition*>(member.get())) {
            for (auto& decl : vd->declarations) {
                if (!decl->id) continue;
                (hasAttribute(vd->attributes, AttributeType::Static) ? out.statics : out.instance).push_back(decl->id->name);
            }
        }
    }
    return out;
}

// What a movie clip has of its own, for a class that extends MovieClip.
const std::vector<std::string>& movieClipMembers() {
    static const std::vector<std::string> names = {
        "_x", "_y", "_xscale", "_yscale", "_alpha", "_visible", "_rotation", "_width", "_height", "_name",
        "_parent", "_currentframe", "_totalframes", "_framesloaded", "_target", "_url", "_droptarget", "_xmouse",
        "_ymouse", "_focusrect", "_lockroot", "_quality", "_highquality", "_soundbuftime", "enabled",
        "useHandCursor", "hitArea", "tabEnabled", "tabIndex", "tabChildren", "trackAsMenu", "menu", "focusEnabled",
        "filters", "blendMode", "cacheAsBitmap", "opaqueBackground", "scrollRect", "scale9Grid", "transform",
        "attachMovie", "createEmptyMovieClip", "createTextField", "duplicateMovieClip", "removeMovieClip",
        "getBounds", "getRect", "getBytesLoaded", "getBytesTotal", "getDepth", "getInstanceAtDepth",
        "getNextHighestDepth", "getURL", "globalToLocal", "localToGlobal", "gotoAndPlay", "gotoAndStop", "hitTest",
        "loadMovie", "loadVariables", "nextFrame", "prevFrame", "play", "stop", "setMask", "startDrag", "stopDrag",
        "swapDepths", "unloadMovie", "beginFill", "beginGradientFill", "beginBitmapFill", "clear", "curveTo",
        "endFill", "lineStyle", "lineGradientStyle", "lineTo", "moveTo", "attachBitmap", "attachAudio",
        "getSWFVersion", "getTextSnapshot", "onEnterFrame", "onLoad", "onUnload", "onPress", "onRelease",
        "onReleaseOutside", "onRollOver", "onRollOut", "onDragOver", "onDragOut", "onMouseDown", "onMouseUp",
        "onMouseMove", "onKeyDown", "onKeyUp", "onData", "onSetFocus", "onKillFocus"};
    return names;
}

class Generator {
public:
    Generator(const Options& options, std::vector<std::string>& errors, const std::string& fileName)
        : _options(options), _errors(errors), _fileName(fileName) {}

    // Top-level code: \a statements as a script, its constant pool first.
    std::vector<uint8_t> script(const std::vector<Statement*>& statements) {
        Pool pool;
        Code code(pool);
        _code = &code;
        Scope top;
        Label exit;
        top.exit = &exit;
        // Registers 1 to 3 are the temporaries a script has, 0 is scratch;
        // a class's code keeps 1 and 2 for its prototype and itself.
        top.freeTemps = _classDefinition ? std::vector<uint8_t>{3} : std::vector<uint8_t>{3, 2, 1};
        _scope = &top;
        if (_classDefinition) {
            compileClassBody(_classDefinition);
        }
        hoist(statements);
        for (Statement* statement : statements) {
            if (!isFunctionDeclaration(statement)) {
                compileStatement(statement);
            }
        }
        code.bind(exit);
        code.action(Action::End);
        _code = nullptr;
        _scope = nullptr;
        if (code.overflowed) {
            error(0, "the script is too long for its jumps");
        }
        std::vector<uint8_t> out;
        if (!pool.empty()) {
            std::vector<uint8_t> payload;
            appendU16(payload, pool.entries().size());
            for (const std::string& entry : pool.entries()) {
                appendString(payload, entry);
            }
            out.push_back(Action::ConstantPool);
            appendU16(out, payload.size());
            out.insert(out.end(), payload.begin(), payload.end());
        }
        out.insert(out.end(), code.bytes.begin(), code.bytes.end());
        return out;
    }

    // An ActionScript 2 class: the code that defines it.
    std::vector<uint8_t> classScript(ClassDefinition* definition, const std::vector<ImportDirective*>& imports,
                                     ClassCode& out) {
        setImports(imports);
        std::string name = definition->name ? definition->name->name : std::string();
        _className = name;
        const size_t dot = name.find_last_of('.');
        _package = dot == std::string::npos ? std::string() : name.substr(0, dot);
        if (dot != std::string::npos) {
            _imports[name.substr(dot + 1)] = name;
        }
        out.name = name;
        out.superName = definition->superClass ? qualify(definition->superClass->name) : std::string();
        if (!out.superName.empty()) {
            out.needs.push_back(out.superName);
        }
        for (const auto& iface : definition->interfaces) {
            if (iface) out.needs.push_back(qualify(iface->name));
        }
        _superName = out.superName;
        gatherMembers(definition);
        std::vector<Statement*> none;
        _classDefinition = definition;
        std::vector<uint8_t> actions = script(none);
        _classDefinition = nullptr;
        return actions;
    }

    // The imports a frame script has, for the short names it uses.
    void setImports(const std::vector<ImportDirective*>& imports) {
        for (ImportDirective* import : imports) {
            if (!import || !import->name) continue;
            if (import->isPackage || import->name->name == "*") {
                _wildcards.push_back(import->path.empty() ? import->name->name : import->path);
            } else {
                _imports[import->name->name] = import->path.empty() ? import->name->name
                                                                    : import->path + "." + import->name->name;
            }
        }
    }

private:
    // The class being compiled, whose definition is the script.
    ClassDefinition* _classDefinition = nullptr;

private:
    const Options& _options;
    std::vector<std::string>& _errors;
    std::string _fileName;
    Code* _code = nullptr;
    Scope* _scope = nullptr;
    int _line = 0;
    // Short names and the qualified ones they stand for (imports and the
    // class's own), and the packages imported whole.
    std::map<std::string, std::string> _imports;
    std::vector<std::string> _wildcards;
    std::string _className;
    std::string _package;
    std::string _superName;
    // In a class's methods: the members its bare names find, instance ones
    // on this and static ones on the class that declares them.
    std::set<std::string> _instanceMembers;
    std::map<std::string, std::string> _staticMembers;

    // The members of \a definition and the classes it extends.
    void gatherMembers(ClassDefinition* definition) {
        const ClassMembers own = membersOf(definition);
        _instanceMembers.insert(own.instance.begin(), own.instance.end());
        for (const std::string& name : own.statics) _staticMembers.emplace(name, _className);
        std::string super = _superName;
        std::set<std::string> seen;
        while (!super.empty() && seen.insert(super).second) {
            const auto found = _options.members.find(super);
            if (found == _options.members.end()) {
                if (super == "MovieClip") {
                    _instanceMembers.insert(movieClipMembers().begin(), movieClipMembers().end());
                }
                break;
            }
            _instanceMembers.insert(found->second.instance.begin(), found->second.instance.end());
            for (const std::string& name : found->second.statics) _staticMembers.emplace(name, super);
            super = found->second.superName;
        }
    }

    // A member found by a bare name, as this.name or Class.name; null when
    // the name is not a member or a local hides it.
    ExpressionPtr memberFor(const std::string& name) {
        if (!_scope || !_scope->function || (_instanceMembers.empty() && _staticMembers.empty())) return nullptr;
        for (Scope* scope = _scope; scope; scope = scope->outer) {
            if (scope->declared.count(name)) return nullptr;
        }
        if (_instanceMembers.count(name)) {
            return std::make_unique<MemberExpression>(std::make_unique<ThisExpression>(),
                                                      std::make_unique<Identifier>(name));
        }
        const auto found = _staticMembers.find(name);
        if (found == _staticMembers.end()) return nullptr;
        // The class by its path: com, then studio, then Hat.
        ExpressionPtr owner;
        size_t start = 0;
        const std::string& path = found->second;
        while (start <= path.size()) {
            const size_t dot = path.find('.', start);
            const std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (!owner) {
                owner = std::make_unique<Identifier>(part);
            } else {
                owner = std::make_unique<MemberExpression>(std::move(owner), std::make_unique<Identifier>(part));
            }
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        return std::make_unique<MemberExpression>(std::move(owner), std::make_unique<Identifier>(name));
    }

    [[noreturn]] void fail(int line, const std::string& message) {
        error(line, message);
        throw Abandon();
    }

    void error(int line, const std::string& message) {
        std::string where = _fileName.empty() ? std::string() : _fileName + ", ";
        where += "line " + std::to_string(line > 0 ? line : _line);
        _errors.push_back(where + ": " + message);
    }

    Code& code() { return *_code; }

    // ---- Names and registers

    uint8_t allocTemp(int line) {
        Scope& scope = *_scope;
        if (!scope.freeTemps.empty()) {
            const uint8_t r = scope.freeTemps.back();
            scope.freeTemps.pop_back();
            return r;
        }
        if (!scope.function || scope.nextRegister > 254) {
            fail(line, "the expression is too deeply nested for the registers there are");
        }
        const uint8_t r = static_cast<uint8_t>(scope.nextRegister++);
        scope.highest = std::max(scope.highest, static_cast<int>(r));
        return r;
    }

    void freeTemp(uint8_t r) { _scope->freeTemps.push_back(r); }

    // A register local, or 0.
    uint8_t registerOf(const std::string& name) const {
        if (!_scope) return 0;
        const auto found = _scope->registers.find(name);
        return found == _scope->registers.end() ? 0 : found->second;
    }

    // The qualified name a class's short name stands for: an import, the
    // class's own package, a package imported whole; or the name as it is.
    std::string qualify(const std::string& name) const {
        const auto imported = _imports.find(name);
        if (imported != _imports.end()) return imported->second;
        if (name.find('.') != std::string::npos) return name;
        const auto known = [this](const std::string& full) {
            return std::find(_options.classes.begin(), _options.classes.end(), full) != _options.classes.end();
        };
        if (!_package.empty() && known(_package + "." + name)) return _package + "." + name;
        for (const std::string& package : _wildcards) {
            if (known(package + "." + name)) return package + "." + name;
        }
        return name;
    }

    // Pushes a qualified name's value: "com.studio.Hat" as com, then its
    // members.
    void pushPath(const std::string& path) {
        size_t start = 0;
        bool first = true;
        while (start <= path.size()) {
            const size_t dot = path.find('.', start);
            const std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            code().pushString(part);
            code().action(first ? Action::GetVariable : Action::GetMember);
            first = false;
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
    }

    // Pushes the value of \a name.
    void loadName(const std::string& name) {
        if (const uint8_t r = registerOf(name)) {
            code().pushRegister(r);
            return;
        }
        if (name == "undefined") {
            code().push(Value::undefined());
            return;
        }
        if (name == "arguments" && _scope->argumentsRegister) {
            code().pushRegister(_scope->argumentsRegister);
            return;
        }
        const std::string path = qualify(name);
        if (path != name) {
            pushPath(path);
            return;
        }
        code().pushString(name);
        code().action(Action::GetVariable);
    }

    void loadThis() {
        if (_scope->thisRegister) {
            code().pushRegister(_scope->thisRegister);
        } else {
            code().pushString("this");
            code().action(Action::GetVariable);
        }
    }

    void loadSuper(int line) {
        if (!_scope->superRegister) {
            fail(line, "super is only for the methods of a class that extends another");
        }
        code().pushRegister(_scope->superRegister);
    }

    // ---- Statements

    static bool isFunctionDeclaration(Statement* statement) {
        return dynamic_cast<FunctionDefinition*>(statement) != nullptr;
    }

    // Function declarations come first, so they can be called from code
    // above them.
    void hoist(const std::vector<Statement*>& statements) {
        for (Statement* statement : statements) {
            if (auto* fd = dynamic_cast<FunctionDefinition*>(statement)) {
                FunctionParts parts;
                parts.name = fd->name ? fd->name->name : std::string();
                parts.parameters = &fd->parameters;
                parts.body = fd->body.get();
                parts.line = fd->line;
                if (fd->isGetter || fd->isSetter) {
                    fail(fd->line, "getters and setters are only for classes");
                }
                compileFunction(parts);
            }
        }
    }

    void hoistBlock(const std::vector<StatementPtr>& statements) {
        std::vector<Statement*> list;
        for (const auto& s : statements) list.push_back(s.get());
        hoist(list);
    }

    void compileStatements(const std::vector<StatementPtr>& statements) {
        for (const auto& statement : statements) {
            if (!isFunctionDeclaration(statement.get())) {
                compileStatement(statement.get());
            }
        }
    }

    void compileStatement(Statement* statement) {
        if (!statement) return;
        if (statement->line > 0) _line = statement->line;
        const int line = statement->line;
        if (auto* block = dynamic_cast<BlockStatement*>(statement)) {
            // A block's functions are declared where it starts.
            hoistBlock(block->statements);
            compileStatements(block->statements);
        } else if (dynamic_cast<EmptyStatement*>(statement)) {
        } else if (auto* es = dynamic_cast<ExpressionStatement*>(statement)) {
            compileDiscarded(es->expression.get());
        } else if (auto* vd = dynamic_cast<VariableDefinition*>(statement)) {
            compileVariables(vd);
        } else if (auto* is = dynamic_cast<IfStatement*>(statement)) {
            compileIf(is);
        } else if (auto* ws = dynamic_cast<WhileStatement*>(statement)) {
            compileWhile(ws, {});
        } else if (auto* ds = dynamic_cast<DoWhileStatement*>(statement)) {
            compileDoWhile(ds, {});
        } else if (auto* fs = dynamic_cast<ForStatement*>(statement)) {
            compileFor(fs, {});
        } else if (auto* fi = dynamic_cast<ForInStatement*>(statement)) {
            compileForIn(fi, {});
        } else if (auto* sw = dynamic_cast<SwitchStatement*>(statement)) {
            compileSwitch(sw, {});
        } else if (auto* ls = dynamic_cast<LabeledStatement*>(statement)) {
            compileLabeled(ls);
        } else if (auto* bs = dynamic_cast<BreakStatement*>(statement)) {
            compileBreak(bs->label ? bs->label->name : std::string(), false, line);
        } else if (auto* cs = dynamic_cast<ContinueStatement*>(statement)) {
            compileBreak(cs->label ? cs->label->name : std::string(), true, line);
        } else if (auto* rs = dynamic_cast<ReturnStatement*>(statement)) {
            compileReturn(rs);
        } else if (auto* ts = dynamic_cast<ThrowStatement*>(statement)) {
            compileExpression(ts->argument.get());
            code().action(Action::Throw);
        } else if (auto* tr = dynamic_cast<TryStatement*>(statement)) {
            compileTry(tr);
        } else if (auto* w = dynamic_cast<WithStatement*>(statement)) {
            compileExpression(w->object.get());
            const size_t at = code().record(Action::With, {0, 0});
            const size_t start = code().size();
            compileStatement(w->body.get());
            code().closePush();
            code().put16(at, code().size() - start);
        } else if (dynamic_cast<FunctionDefinition*>(statement)) {
            // Declared already (hoisted).
        } else if (dynamic_cast<ClassDefinition*>(statement)) {
            fail(line, "a class goes in a class file of its own");
        } else {
            fail(line, "this statement is not in ActionScript 2");
        }
    }

    void compileVariables(VariableDefinition* vd) {
        for (auto& decl : vd->declarations) {
            if (!decl->id) continue;
            const std::string& name = decl->id->name;
            if (const uint8_t r = registerOf(name)) {
                if (decl->init) {
                    compileExpression(decl->init.get());
                    code().storeRegister(r);
                    code().action(Action::Pop);
                }
                continue;
            }
            code().pushString(name);
            if (decl->init) {
                compileExpression(decl->init.get());
                code().action(Action::DefineLocal);
            } else {
                code().action(Action::DefineLocal2);
            }
        }
    }

    void compileIf(IfStatement* is) {
        Label otherwise, end;
        compileExpression(is->test.get());
        code().action(Action::Not);
        code().jump(Action::If, otherwise);
        compileStatement(is->consequent.get());
        if (is->alternate) {
            code().jump(Action::Jump, end);
            code().bind(otherwise);
            compileStatement(is->alternate.get());
            code().bind(end);
        } else {
            code().bind(otherwise);
        }
    }

    void loopBody(Statement* body, const std::vector<std::string>& labels, Label& breakTo, Label& continueTo,
                  bool forIn = false) {
        Breakable entry;
        entry.labels = labels;
        entry.breakTo = &breakTo;
        entry.continueTo = &continueTo;
        entry.forIn = forIn;
        entry.loop = true;
        _scope->breakables.push_back(entry);
        compileStatement(body);
        _scope->breakables.pop_back();
    }

    void compileWhile(WhileStatement* ws, const std::vector<std::string>& labels) {
        Label top, end;
        code().bind(top);
        compileExpression(ws->test.get());
        code().action(Action::Not);
        code().jump(Action::If, end);
        loopBody(ws->body.get(), labels, end, top);
        code().jump(Action::Jump, top);
        code().bind(end);
    }

    void compileDoWhile(DoWhileStatement* ds, const std::vector<std::string>& labels) {
        Label top, next, end;
        code().bind(top);
        loopBody(ds->body.get(), labels, end, next);
        code().bind(next);
        compileExpression(ds->test.get());
        code().jump(Action::If, top);
        code().bind(end);
    }

    void compileFor(ForStatement* fs, const std::vector<std::string>& labels) {
        Label top, next, end;
        if (fs->init) compileStatement(fs->init.get());
        code().bind(top);
        if (fs->test) {
            compileExpression(fs->test.get());
            code().action(Action::Not);
            code().jump(Action::If, end);
        }
        loopBody(fs->body.get(), labels, end, next);
        code().bind(next);
        if (fs->update) compileDiscarded(fs->update.get());
        code().jump(Action::Jump, top);
        code().bind(end);
    }

    // The names Enumerate2 left on the stack, popped down to and with the
    // null under them.
    void popEnumeration() {
        Label again;
        code().bind(again);
        code().push(Value::null());
        code().action(Action::Equals2);
        code().action(Action::Not);
        code().jump(Action::If, again);
    }

    void compileForIn(ForInStatement* fi, const std::vector<std::string>& labels) {
        if (fi->each) {
            fail(fi->line, "for each is not in ActionScript 2");
        }
        // The target each name goes into.
        std::string variable;
        Expression* target = nullptr;
        ExpressionPtr member;
        bool declared = false;
        if (auto* vd = dynamic_cast<VariableDefinition*>(fi->left.get())) {
            if (vd->declarations.size() != 1 || !vd->declarations[0]->id) {
                fail(fi->line, "for-in takes one variable");
            }
            variable = vd->declarations[0]->id->name;
            declared = true;
        } else if (auto* es = dynamic_cast<ExpressionStatement*>(fi->left.get())) {
            target = es->expression.get();
            if (auto* id = dynamic_cast<Identifier*>(target)) {
                variable = id->name;
                target = nullptr;
                if ((member = memberFor(variable))) {
                    target = member.get();
                    variable.clear();
                }
            }
        } else {
            fail(fi->line, "for-in needs a variable");
        }
        compileExpression(fi->right.get());
        code().action(Action::Enumerate2);
        Label top, end, cleanup, done;
        code().bind(top);
        code().storeRegister(0);
        code().push(Value::null());
        code().action(Action::Equals2);
        code().jump(Action::If, end);
        if (!variable.empty()) {
            if (const uint8_t r = registerOf(variable)) {
                code().pushRegister(0);
                code().storeRegister(r);
                code().action(Action::Pop);
            } else {
                code().pushString(variable);
                code().pushRegister(0);
                code().action(declared ? Action::DefineLocal : Action::SetVariable);
            }
        } else if (auto* into = dynamic_cast<MemberExpression*>(target)) {
            compileExpression(into->object.get());
            pushKey(into);
            code().pushRegister(0);
            code().action(Action::SetMember);
        } else {
            fail(fi->line, "for-in cannot assign to that");
        }
        loopBody(fi->body.get(), labels, cleanup, top, true);
        code().jump(Action::Jump, top);
        // A break leaves the names still to come on the stack.
        code().bind(cleanup);
        popEnumeration();
        code().bind(end);
        code().bind(done);
    }

    void compileSwitch(SwitchStatement* sw, const std::vector<std::string>& labels) {
        // The value is kept on the stack while the cases are tried, each
        // with a copy, and popped on the way to the case that matches.
        compileExpression(sw->discriminant.get());
        std::vector<Label> matched(sw->cases.size());
        std::vector<Label> bodies(sw->cases.size());
        Label end;
        int defaultCase = -1;
        for (size_t i = 0; i < sw->cases.size(); ++i) {
            if (!sw->cases[i]->test) {
                defaultCase = static_cast<int>(i);
                continue;
            }
            code().action(Action::PushDuplicate);
            compileExpression(sw->cases[i]->test.get());
            code().action(Action::StrictEquals);
            code().jump(Action::If, matched[i]);
        }
        code().action(Action::Pop);
        if (defaultCase >= 0) {
            code().jump(Action::Jump, bodies[static_cast<size_t>(defaultCase)]);
        } else {
            code().jump(Action::Jump, end);
        }
        for (size_t i = 0; i < sw->cases.size(); ++i) {
            if (!sw->cases[i]->test) continue;
            code().bind(matched[i]);
            code().action(Action::Pop);
            code().jump(Action::Jump, bodies[i]);
        }
        Breakable entry;
        entry.labels = labels;
        entry.breakTo = &end;
        entry.isSwitch = true;
        _scope->breakables.push_back(entry);
        for (size_t i = 0; i < sw->cases.size(); ++i) {
            code().bind(bodies[i]);
            hoistBlock(sw->cases[i]->consequent);
            compileStatements(sw->cases[i]->consequent);
        }
        _scope->breakables.pop_back();
        code().bind(end);
    }

    void compileLabeled(LabeledStatement* ls) {
        std::vector<std::string> labels;
        Statement* body = ls;
        while (auto* labeled = dynamic_cast<LabeledStatement*>(body)) {
            if (labeled->label) labels.push_back(labeled->label->name);
            body = labeled->body.get();
        }
        if (auto* ws = dynamic_cast<WhileStatement*>(body)) {
            compileWhile(ws, labels);
        } else if (auto* ds = dynamic_cast<DoWhileStatement*>(body)) {
            compileDoWhile(ds, labels);
        } else if (auto* fs = dynamic_cast<ForStatement*>(body)) {
            compileFor(fs, labels);
        } else if (auto* fi = dynamic_cast<ForInStatement*>(body)) {
            compileForIn(fi, labels);
        } else if (auto* sw = dynamic_cast<SwitchStatement*>(body)) {
            compileSwitch(sw, labels);
        } else {
            Label end;
            Breakable entry;
            entry.labels = labels;
            entry.breakTo = &end;
            _scope->breakables.push_back(entry);
            compileStatement(body);
            _scope->breakables.pop_back();
            code().bind(end);
        }
    }

    void compileBreak(const std::string& label, bool isContinue, int line) {
        std::vector<Breakable>& stack = _scope->breakables;
        for (size_t i = stack.size(); i-- > 0;) {
            Breakable& entry = stack[i];
            // A bare break leaves the innermost loop or switch, a bare
            // continue the innermost loop; a label names its statement.
            const bool fits = !label.empty()
                ? std::find(entry.labels.begin(), entry.labels.end(), label) != entry.labels.end()
                : isContinue ? entry.loop : entry.loop || entry.isSwitch;
            if (!fits) continue;
            if (isContinue && !entry.loop) {
                fail(line, "continue needs a loop");
            }
            // The for-in loops left on the way have their names popped; a
            // for-in's own break goes to where it pops them. A finally block
            // on the way runs first.
            for (size_t k = stack.size(); k-- > i + 1;) {
                if (stack[k].finally) {
                    leaveTry(stack[k], {isContinue ? TryExit::Continue : TryExit::Break, label, line});
                    return;
                }
                if (stack[k].forIn) popEnumeration();
            }
            code().jump(Action::Jump, isContinue ? *entry.continueTo : *entry.breakTo);
            return;
        }
        fail(line, isContinue ? "continue needs a loop" : "break needs a loop or a switch");
    }

    // Leaves the try block (or catch block) of \a entry for \a exit: the
    // way out noted, and on to the block's end, where the finally block
    // runs and then takes it (see compileTry).
    void leaveTry(Breakable& entry, const TryExit& exit) {
        std::vector<TryExit>& exits = *entry.exits;
        size_t index = 0;
        while (index < exits.size() && !(exits[index].kind == exit.kind && exits[index].label == exit.label)) ++index;
        if (index == exits.size()) exits.push_back(exit);
        code().pushNumber(static_cast<double>(index + 1));
        code().storeRegister(entry.exitRegister);
        code().action(Action::Pop);
        code().jump(Action::Jump, *entry.blockEnd);
    }

    // The innermost try block with a finally block the code is in, or -1.
    int innermostFinally() const {
        for (size_t k = _scope->breakables.size(); k-- > 0;) {
            if (_scope->breakables[k].finally) return static_cast<int>(k);
        }
        return -1;
    }

    void compileReturn(ReturnStatement* rs) {
        if (!_scope->function) {
            // At the top level, the rest of the script is passed over.
            if (rs->argument) compileDiscarded(rs->argument.get());
            returnFromScript(rs->line);
            return;
        }
        if (rs->argument) {
            compileExpression(rs->argument.get());
        } else {
            code().push(Value::undefined());
        }
        returnValue(rs->line);
    }

    // A top-level return: past the rest of the script, after the finally
    // blocks it is in.
    void returnFromScript(int line) {
        std::vector<Breakable>& stack = _scope->breakables;
        for (size_t k = stack.size(); k-- > 0;) {
            if (stack[k].finally) {
                leaveTry(stack[k], {TryExit::Return, std::string(), line});
                return;
            }
            if (stack[k].forIn) popEnumeration();
        }
        code().jump(Action::Jump, *_scope->exit);
    }

    // Returns the value on the stack, after the finally blocks it is in:
    // kept in a register while one runs.
    void returnValue(int line) {
        const int f = innermostFinally();
        if (f >= 0) {
            Breakable& entry = _scope->breakables[static_cast<size_t>(f)];
            code().storeRegister(entry.valueRegister);
            code().action(Action::Pop);
            for (size_t k = _scope->breakables.size(); k-- > static_cast<size_t>(f) + 1;) {
                if (_scope->breakables[k].forIn) popEnumeration();
            }
            leaveTry(entry, {TryExit::Return, std::string(), line});
            return;
        }
        const bool inForIn = std::any_of(_scope->breakables.begin(), _scope->breakables.end(),
                                         [](const Breakable& entry) { return entry.forIn; });
        if (inForIn) {
            code().storeRegister(0);
            code().action(Action::Pop);
            for (size_t k = _scope->breakables.size(); k-- > 0;) {
                if (_scope->breakables[k].forIn) popEnumeration();
            }
            code().pushRegister(0);
        }
        code().action(Action::Return);
    }

    void compileTry(TryStatement* tr) {
        // One catch block: an untyped catch takes the error by name; typed
        // ones (or several) take it in a register and try each type in turn.
        const bool hasCatch = !tr->catches.empty();
        const bool hasFinally = tr->finallyBody != nullptr;
        bool typed = tr->catches.size() > 1;
        for (const CatchClause& clause : tr->catches) {
            if (!clause.type.empty() && clause.type != "*" && clause.type != "Object") typed = true;
        }
        uint8_t caught = 0;
        std::vector<uint8_t> payload;
        uint8_t flags = 0;
        if (hasCatch) flags |= 0x01;
        if (hasFinally) flags |= 0x02;
        if (typed) {
            caught = allocTemp(tr->line);
            flags |= 0x04;
        }
        payload.push_back(flags);
        appendU16(payload, 0);
        appendU16(payload, 0);
        appendU16(payload, 0);
        if (typed) {
            payload.push_back(caught);
        } else {
            appendString(payload, hasCatch && tr->catches[0].variable ? tr->catches[0].variable->name : std::string());
        }
        // A break, continue or return out of the try or catch block goes
        // to the block's end, so the finally block runs, and is taken after
        // it: which, in a register (0 for none); a return's value in another.
        std::vector<TryExit> exits;
        Label tryEnd, catchEnd;
        Breakable leaving;
        const bool routed = hasFinally &&
            (jumpsIn(tr->block.get()) ||
             std::any_of(tr->catches.begin(), tr->catches.end(),
                         [](const CatchClause& clause) { return jumpsIn(clause.body.get()); }));
        if (routed) {
            leaving.finally = true;
            leaving.exits = &exits;
            leaving.exitRegister = allocTemp(tr->line);
            if (_scope->function) leaving.valueRegister = allocTemp(tr->line);
            code().pushNumber(0);
            code().storeRegister(leaving.exitRegister);
            code().action(Action::Pop);
        }
        const size_t at = code().record(Action::Try, payload);
        // The player passes over the catch block when nothing is thrown.
        const size_t tryStart = code().size();
        if (routed) {
            leaving.blockEnd = &tryEnd;
            _scope->breakables.push_back(leaving);
        }
        compileStatement(tr->block.get());
        if (routed) _scope->breakables.pop_back();
        code().closePush();
        code().bind(tryEnd);
        const size_t trySize = code().size() - tryStart;
        const size_t catchStart = code().size();
        if (routed && hasCatch) {
            leaving.blockEnd = &catchEnd;
            _scope->breakables.push_back(leaving);
        }
        if (hasCatch) {
            if (!typed) {
                compileStatement(tr->catches[0].body.get());
            } else {
                Label done;
                bool untyped = false;
                for (const CatchClause& clause : tr->catches) {
                    Label next;
                    const bool any = clause.type.empty() || clause.type == "*" || clause.type == "Object";
                    if (!any) {
                        code().pushRegister(caught);
                        pushPath(qualify(clause.type));
                        code().action(Action::InstanceOf);
                        code().action(Action::Not);
                        code().jump(Action::If, next);
                    }
                    if (clause.variable) {
                        code().pushString(clause.variable->name);
                        code().pushRegister(caught);
                        code().action(_scope->function ? Action::DefineLocal : Action::SetVariable);
                    }
                    compileStatement(clause.body.get());
                    code().jump(Action::Jump, done);
                    code().bind(next);
                    if (any) {
                        untyped = true;
                        break;
                    }
                }
                if (!untyped) {
                    // No clause takes it: thrown on.
                    code().pushRegister(caught);
                    code().action(Action::Throw);
                }
                code().bind(done);
            }
        }
        if (routed && hasCatch) _scope->breakables.pop_back();
        code().closePush();
        code().bind(catchEnd);
        const size_t catchSize = code().size() - catchStart;
        const size_t finallyStart = code().size();
        if (hasFinally) {
            compileStatement(tr->finallyBody.get());
        }
        code().closePush();
        const size_t finallySize = code().size() - finallyStart;
        code().put16(at + 1, trySize);
        code().put16(at + 3, catchSize);
        code().put16(at + 5, finallySize);
        if (typed) freeTemp(caught);
        // The ways out taken once the finally block has run, through the
        // finally blocks further out.
        for (size_t i = 0; i < exits.size(); ++i) {
            Label next;
            code().pushRegister(leaving.exitRegister);
            code().pushNumber(static_cast<double>(i + 1));
            code().action(Action::Equals2);
            code().action(Action::Not);
            code().jump(Action::If, next);
            const TryExit& exit = exits[i];
            if (exit.kind == TryExit::Return) {
                if (_scope->function) {
                    code().pushRegister(leaving.valueRegister);
                    returnValue(exit.line);
                } else {
                    returnFromScript(exit.line);
                }
            } else {
                compileBreak(exit.label, exit.kind == TryExit::Continue, exit.line);
            }
            code().bind(next);
        }
        if (routed) {
            if (leaving.valueRegister) freeTemp(leaving.valueRegister);
            freeTemp(leaving.exitRegister);
        }
    }

    // ---- Functions

    void compileFunction(const FunctionParts& parts) {
        Usage usage;
        const std::function<bool(const std::string&)> isMember = [this](const std::string& name) {
            return _instanceMembers.count(name) > 0;
        };
        if (!_instanceMembers.empty()) usage.member = &isMember;
        usageIn(parts.body, usage);
        if (parts.callSuper) usage.usesSuper = true;
        for (const auto& p : *parts.parameters) {
            usageIn(p->defaultValue.get(), usage);
        }
        Scope scope;
        scope.function = true;
        uint16_t flags = 0;
        int next = 1;
        if (usage.usesThis) {
            flags |= PreloadThis;
            scope.thisRegister = static_cast<uint8_t>(next++);
        } else {
            flags |= SuppressThis;
        }
        if (usage.usesArguments) {
            flags |= PreloadArguments;
            scope.argumentsRegister = static_cast<uint8_t>(next++);
        } else {
            flags |= SuppressArguments;
        }
        if (usage.usesSuper) {
            flags |= PreloadSuper;
            scope.superRegister = static_cast<uint8_t>(next++);
        } else {
            flags |= SuppressSuper;
        }
        const auto inRegister = [&](const std::string& name) {
            return !usage.dynamic && !usage.captured.count(name) && !usage.declaredFunctions.count(name) &&
                name != "arguments" && name != "this";
        };
        // The parameters, in registers where they can be.
        std::vector<std::pair<uint8_t, std::string>> params;
        for (const auto& p : *parts.parameters) {
            const std::string name = p->name ? p->name->name : std::string();
            if (p->rest) {
                fail(parts.line, "rest parameters are not in ActionScript 2");
            }
            if (inRegister(name) && next < 255) {
                const uint8_t r = static_cast<uint8_t>(next++);
                scope.registers[name] = r;
                params.emplace_back(r, name);
            } else {
                params.emplace_back(0, name);
            }
        }
        for (const std::string& local : usage.locals) {
            if (scope.registers.count(local)) continue;
            bool isParam = false;
            for (const auto& p : params) isParam = isParam || p.second == local;
            if (isParam) continue;
            if (inRegister(local) && next < 255) {
                scope.registers[local] = static_cast<uint8_t>(next++);
            }
        }
        scope.nextRegister = next;
        scope.highest = next - 1;
        scope.outer = _scope;
        for (const auto& p : params) scope.declared.insert(p.second);
        scope.declared.insert(usage.locals.begin(), usage.locals.end());
        scope.declared.insert(usage.declaredFunctions.begin(), usage.declaredFunctions.end());
        scope.declared.insert(usage.catches.begin(), usage.catches.end());

        std::vector<uint8_t> payload;
        appendString(payload, parts.name);
        appendU16(payload, params.size());
        payload.push_back(0); // the register count, set once known
        payload.push_back(static_cast<uint8_t>(flags & 0xFF));
        payload.push_back(static_cast<uint8_t>(flags >> 8));
        for (const auto& p : params) {
            payload.push_back(p.first);
            appendString(payload, p.second);
        }
        appendU16(payload, 0); // the code size, set once known
        const size_t at = code().record(Action::DefineFunction2, payload);
        const size_t countAt = at + parts.name.size() + 1 + 2;
        const size_t sizeAt = at + payload.size() - 2;
        const size_t start = code().size();

        Scope* outer = _scope;
        _scope = &scope;
        // Default values for parameters left out.
        for (const auto& p : *parts.parameters) {
            if (!p->defaultValue || !p->name) continue;
            Label given;
            loadName(p->name->name);
            code().push(Value::undefined());
            code().action(Action::StrictEquals);
            code().action(Action::Not);
            code().jump(Action::If, given);
            Expression* value = p->defaultValue.get();
            storeName(p->name->name, [&]() { compileExpression(value); }, true);
            code().bind(given);
        }
        if (parts.callSuper) {
            code().pushNumber(0);
            code().pushRegister(scope.superRegister);
            code().push(Value::undefined());
            code().action(Action::CallMethod);
            code().action(Action::Pop);
        }
        if (auto* block = dynamic_cast<BlockStatement*>(parts.body)) {
            hoistBlock(block->statements);
            compileStatements(block->statements);
        } else if (parts.body) {
            compileStatement(parts.body);
        }
        _scope = outer;
        code().closePush();
        const size_t size = code().size() - start;
        if (size > 65535) {
            fail(parts.line, "the function is too long");
        }
        code().put16(sizeAt, size);
        code().bytes[countAt] = static_cast<uint8_t>(std::min(255, scope.highest + 1));
    }

    void storeName(const std::string& name, const std::function<void()>& value, bool define) {
        if (const uint8_t r = registerOf(name)) {
            value();
            code().storeRegister(r);
            code().action(Action::Pop);
            return;
        }
        code().pushString(name);
        value();
        code().action(define ? Action::DefineLocal : Action::SetVariable);
    }

    // ---- Classes

    // Makes the packages a qualified name is in, as empty objects on _global.
    void makePackages(const std::string& name) {
        std::string path;
        size_t start = 0;
        while (true) {
            const size_t dot = name.find('.', start);
            if (dot == std::string::npos) break;
            const std::string part = name.substr(start, dot - start);
            Label have;
            pushGlobalPath(path);
            code().pushString(part);
            code().action(Action::GetMember);
            code().jump(Action::If, have);
            pushGlobalPath(path);
            code().pushString(part);
            code().pushNumber(0);
            code().pushString("Object");
            code().action(Action::NewObject);
            code().action(Action::SetMember);
            code().bind(have);
            path = path.empty() ? part : path + "." + part;
            start = dot + 1;
        }
    }

    // _global, then the members along \a path.
    void pushGlobalPath(const std::string& path) {
        code().pushString("_global");
        code().action(Action::GetVariable);
        size_t start = 0;
        while (!path.empty() && start <= path.size()) {
            const size_t dot = path.find('.', start);
            code().pushString(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
            code().action(Action::GetMember);
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
    }

    // The object that holds a qualified name's last part, and that part.
    void pushOwner(const std::string& name, std::string& last) {
        const size_t dot = name.find_last_of('.');
        last = dot == std::string::npos ? name : name.substr(dot + 1);
        pushGlobalPath(dot == std::string::npos ? std::string() : name.substr(0, dot));
    }

    void compileClassBody(ClassDefinition* definition) {
        const std::string& name = _className;
        const int line = definition->line;
        if (name.empty()) fail(line, "the class has no name");
        makePackages(name);
        Label defined;
        std::string last;
        pushOwner(name, last);
        code().pushString(last);
        code().action(Action::GetMember);
        code().jump(Action::If, defined);

        if (definition->isInterface) {
            // An interface is a function of its own, for instanceof.
            pushOwner(name, last);
            code().pushString(last);
            FunctionParts parts;
            static const std::vector<std::unique_ptr<Parameter>> none;
            parts.parameters = &none;
            parts.line = line;
            compileFunction(parts);
            code().action(Action::SetMember);
            code().bind(defined);
            return;
        }

        // The constructor: the class's own function.
        FunctionDefinition* constructor = nullptr;
        std::string shortName = name.substr(name.find_last_of('.') == std::string::npos ? 0 : name.find_last_of('.') + 1);
        for (auto& member : definition->members) {
            auto* fd = dynamic_cast<FunctionDefinition*>(member.get());
            if (fd && fd->name && fd->name->name == shortName && !hasAttribute(fd->attributes, AttributeType::Static)) {
                constructor = fd;
            }
        }
        pushOwner(name, last);
        code().pushString(last);
        {
            FunctionParts parts;
            static const std::vector<std::unique_ptr<Parameter>> none;
            parts.parameters = constructor ? &constructor->parameters : &none;
            parts.body = constructor ? constructor->body.get() : nullptr;
            parts.line = constructor ? constructor->line : line;
            parts.callSuper = !_superName.empty() && !(constructor && callsSuper(constructor->body.get()));
            compileFunction(parts);
        }
        code().action(Action::SetMember);

        // It extends the class it names: its prototype one of that's.
        if (!_superName.empty()) {
            pushPath(name);
            pushPath(_superName);
            code().action(Action::Extends);
        }
        // Its prototype in a register while the methods go on it.
        const uint8_t proto = 1;
        pushPath(name);
        code().pushString("prototype");
        code().action(Action::GetMember);
        code().storeRegister(proto);
        code().action(Action::Pop);
        const uint8_t self = 2;
        pushPath(name);
        code().storeRegister(self);
        code().action(Action::Pop);

        std::set<std::string> instanceAccessors;
        std::set<std::string> staticAccessors;
        for (auto& member : definition->members) {
            if (auto* fd = dynamic_cast<FunctionDefinition*>(member.get())) {
                if (fd == constructor || !fd->name) continue;
                const bool isStatic = hasAttribute(fd->attributes, AttributeType::Static);
                std::string method = fd->name->name;
                if (fd->isGetter || fd->isSetter) {
                    (isStatic ? staticAccessors : instanceAccessors).insert(method);
                    method = (fd->isGetter ? "__get__" : "__set__") + method;
                }
                if (!fd->body) continue;
                code().pushRegister(isStatic ? self : proto);
                code().pushString(method);
                FunctionParts parts;
                parts.parameters = &fd->parameters;
                parts.body = fd->body.get();
                parts.line = fd->line;
                compileFunction(parts);
                code().action(Action::SetMember);
            } else if (auto* vd = dynamic_cast<VariableDefinition*>(member.get())) {
                const bool isStatic = hasAttribute(vd->attributes, AttributeType::Static);
                for (auto& decl : vd->declarations) {
                    // Instance values go on the prototype, as ActionScript 2
                    // has them.
                    if (!decl->id || !decl->init) continue;
                    code().pushRegister(isStatic ? self : proto);
                    code().pushString(decl->id->name);
                    compileExpression(decl->init.get());
                    code().action(Action::SetMember);
                }
            } else if (auto* statement = dynamic_cast<Statement*>(member.get())) {
                if (!dynamic_cast<EmptyStatement*>(statement)) {
                    fail(statement->line, "a class has only variables and functions");
                }
            }
        }
        // Getters and setters as properties: addProperty(name, get, set).
        const auto addProperties = [&](const std::set<std::string>& names, uint8_t on) {
            for (const std::string& property : names) {
                // The setter, the getter and the name, pushed in reverse.
                code().pushRegister(on);
                code().pushString("__set__" + property);
                code().action(Action::GetMember);
                code().pushRegister(on);
                code().pushString("__get__" + property);
                code().action(Action::GetMember);
                code().pushString(property);
                code().pushNumber(3);
                code().pushRegister(on);
                code().pushString("addProperty");
                code().action(Action::CallMethod);
                code().action(Action::Pop);
            }
        };
        addProperties(instanceAccessors, proto);
        addProperties(staticAccessors, self);
        // The prototype's members hidden from for-in.
        code().pushNumber(1);
        code().push(Value::null());
        code().pushRegister(proto);
        code().pushNumber(3);
        code().pushString("ASSetPropFlags");
        code().action(Action::CallFunction);
        code().action(Action::Pop);
        code().bind(defined);
    }

    static bool callsSuper(AstNode* node) {
        if (!node) return false;
        if (auto* call = dynamic_cast<CallExpression*>(node)) {
            if (dynamic_cast<SuperExpression*>(call->callee.get())) return true;
        }
        if (auto* es = dynamic_cast<ExpressionStatement*>(node)) return callsSuper(es->expression.get());
        if (auto* block = dynamic_cast<BlockStatement*>(node)) {
            for (auto& s : block->statements) {
                if (callsSuper(s.get())) return true;
            }
        }
        return false;
    }

    // ---- Expressions

    void compileDiscarded(Expression* expr) {
        if (!expr) return;
        if (expr->line > 0) _line = expr->line;
        switch (expr->kind) {
            case Expression::ExpressionKind::Assignment:
                compileAssignment(static_cast<AssignmentExpression*>(expr), true);
                return;
            case Expression::ExpressionKind::Unary: {
                auto* u = static_cast<UnaryExpression*>(expr);
                if (u->op == OperatorType::PreIncrement || u->op == OperatorType::PostIncrement ||
                    u->op == OperatorType::PreDecrement || u->op == OperatorType::PostDecrement) {
                    compileIncrement(u->operand.get(),
                                     u->op == OperatorType::PreIncrement || u->op == OperatorType::PostIncrement,
                                     false, true, u->line);
                    return;
                }
                break;
            }
            case Expression::ExpressionKind::Call:
                if (compileCall(static_cast<CallExpression*>(expr), true)) return;
                code().action(Action::Pop);
                return;
            case Expression::ExpressionKind::Sequence:
                for (auto& e : static_cast<SequenceExpression*>(expr)->expressions) compileDiscarded(e.get());
                return;
            default:
                break;
        }
        compileExpression(expr);
        code().action(Action::Pop);
    }

    void compileExpression(Expression* expr) {
        if (!expr) {
            code().push(Value::undefined());
            return;
        }
        if (expr->line > 0) _line = expr->line;
        const int line = expr->line;
        switch (expr->kind) {
            case Expression::ExpressionKind::Identifier: {
                const std::string& name = static_cast<Identifier*>(expr)->name;
                if (ExpressionPtr member = memberFor(name)) {
                    compileExpression(member.get());
                    return;
                }
                loadName(name);
                return;
            }
            case Expression::ExpressionKind::Literal:
                compileLiteral(static_cast<Literal*>(expr));
                return;
            case Expression::ExpressionKind::This:
                loadThis();
                return;
            case Expression::ExpressionKind::Super:
                loadSuper(line);
                return;
            case Expression::ExpressionKind::Binary:
                compileBinary(static_cast<BinaryExpression*>(expr));
                return;
            case Expression::ExpressionKind::Unary:
                compileUnary(static_cast<UnaryExpression*>(expr));
                return;
            case Expression::ExpressionKind::Conditional: {
                auto* c = static_cast<ConditionalExpression*>(expr);
                Label yes, end;
                compileExpression(c->test.get());
                code().jump(Action::If, yes);
                compileExpression(c->alternate.get());
                code().jump(Action::Jump, end);
                code().bind(yes);
                compileExpression(c->consequent.get());
                code().bind(end);
                return;
            }
            case Expression::ExpressionKind::Assignment:
                compileAssignment(static_cast<AssignmentExpression*>(expr), false);
                return;
            case Expression::ExpressionKind::Call:
                compileCall(static_cast<CallExpression*>(expr), false);
                return;
            case Expression::ExpressionKind::New:
                compileNew(static_cast<NewExpression*>(expr));
                return;
            case Expression::ExpressionKind::Member: {
                auto* m = static_cast<MemberExpression*>(expr);
                std::string path;
                if (qualifiedPath(m, path)) {
                    pushPath(path);
                    return;
                }
                compileExpression(m->object.get());
                pushKey(m);
                code().action(Action::GetMember);
                return;
            }
            case Expression::ExpressionKind::Function: {
                auto* f = static_cast<FunctionExpression*>(expr);
                FunctionParts parts;
                parts.parameters = &f->parameters;
                parts.body = f->body.get();
                parts.line = f->line;
                compileFunction(parts);
                // A named function expression is its name's too.
                return;
            }
            case Expression::ExpressionKind::Array: {
                auto* a = static_cast<ArrayExpression*>(expr);
                if (!a->vectorType.empty()) fail(line, "vectors are not in ActionScript 2");
                for (size_t i = a->elements.size(); i-- > 0;) {
                    compileExpression(a->elements[i].get());
                }
                code().pushNumber(static_cast<double>(a->elements.size()));
                code().action(Action::InitArray);
                return;
            }
            case Expression::ExpressionKind::Object: {
                auto* o = static_cast<ObjectExpression*>(expr);
                for (size_t i = o->properties.size(); i-- > 0;) {
                    auto& property = o->properties[i];
                    Expression* key = property.first.get();
                    if (auto* id = dynamic_cast<Identifier*>(key)) {
                        code().pushString(id->name);
                    } else if (auto* literal = dynamic_cast<Literal*>(key)) {
                        if (literal->kind == Literal::LiteralKind::Numeric) {
                            code().pushString(numberText(literal->numberValue));
                        } else {
                            code().pushString(literal->stringValue);
                        }
                    } else {
                        fail(line, "an object's names are words, strings or numbers");
                    }
                    compileExpression(property.second.get());
                }
                code().pushNumber(static_cast<double>(o->properties.size()));
                code().action(Action::InitObject);
                return;
            }
            case Expression::ExpressionKind::Sequence: {
                auto* s = static_cast<SequenceExpression*>(expr);
                for (size_t i = 0; i < s->expressions.size(); ++i) {
                    if (i + 1 < s->expressions.size()) {
                        compileDiscarded(s->expressions[i].get());
                    } else {
                        compileExpression(s->expressions[i].get());
                    }
                }
                if (s->expressions.empty()) code().push(Value::undefined());
                return;
            }
            default:
                fail(line, "this expression is not in ActionScript 2");
        }
    }

    static std::string numberText(double value) {
        if (value == std::floor(value) && std::fabs(value) < 1e15) {
            std::ostringstream out;
            out << static_cast<long long>(value);
            return out.str();
        }
        std::ostringstream out;
        out.precision(15);
        out << value;
        return out.str();
    }

    void compileLiteral(Literal* literal) {
        switch (literal->kind) {
            case Literal::LiteralKind::Null:
                code().push(Value::null());
                return;
            case Literal::LiteralKind::Undefined:
                code().push(Value::undefined());
                return;
            case Literal::LiteralKind::Boolean:
                code().push(Value::boolean_(literal->boolValue));
                return;
            case Literal::LiteralKind::Numeric:
                code().pushNumber(literal->numberValue);
                return;
            case Literal::LiteralKind::String:
                code().pushString(literal->stringValue);
                return;
            default:
                fail(literal->line, "this literal is not in ActionScript 2");
        }
    }

    // a.b.C as a class's qualified name, when a names nothing local and
    // the path is one the movie's classes have.
    bool qualifiedPath(MemberExpression* m, std::string& path) {
        std::vector<std::string> parts;
        Expression* at = m;
        while (auto* member = dynamic_cast<MemberExpression*>(at)) {
            if (member->computed || !member->property) return false;
            parts.push_back(member->property->name);
            at = member->object.get();
        }
        auto* root = dynamic_cast<Identifier*>(at);
        if (!root || registerOf(root->name)) return false;
        // Only an import's short name changes what is pushed.
        const std::string qualified = qualify(root->name);
        if (qualified == root->name) return false;
        path = qualified;
        for (size_t i = parts.size(); i-- > 0;) path += "." + parts[i];
        return true;
    }

    // A member's name or computed key.
    void pushKey(MemberExpression* m) {
        if (m->computed) {
            compileExpression(m->computedProperty.get());
        } else {
            code().pushString(m->property ? m->property->name : std::string());
        }
    }

    // An expression that can be evaluated twice without a difference.
    bool simple(Expression* expr) const {
        if (!expr) return true;
        switch (expr->kind) {
            case Expression::ExpressionKind::Identifier:
            case Expression::ExpressionKind::Literal:
            case Expression::ExpressionKind::This:
            case Expression::ExpressionKind::Super:
                return true;
            case Expression::ExpressionKind::Member: {
                auto* m = static_cast<MemberExpression*>(expr);
                return simple(m->object.get()) && (!m->computed || simple(m->computedProperty.get()));
            }
            default:
                return false;
        }
    }

    bool emitBinary(OperatorType op, int line) {
        switch (op) {
            case OperatorType::Add: code().action(Action::Add2); return true;
            case OperatorType::Subtract: code().action(Action::Subtract); return true;
            case OperatorType::Multiply: code().action(Action::Multiply); return true;
            case OperatorType::Divide: code().action(Action::Divide); return true;
            case OperatorType::Remainder: code().action(Action::Modulo); return true;
            case OperatorType::ShiftLeft: code().action(Action::BitLShift); return true;
            case OperatorType::ShiftRight: code().action(Action::BitRShift); return true;
            case OperatorType::ShiftRightUnsigned: code().action(Action::BitURShift); return true;
            case OperatorType::BitwiseAnd: code().action(Action::BitAnd); return true;
            case OperatorType::BitwiseOr: code().action(Action::BitOr); return true;
            case OperatorType::BitwiseXor: code().action(Action::BitXor); return true;
            case OperatorType::Lt: code().action(Action::Less2); return true;
            case OperatorType::Gt: code().action(Action::Greater); return true;
            case OperatorType::Le:
                code().action(Action::Greater);
                code().action(Action::Not);
                return true;
            case OperatorType::Ge:
                code().action(Action::Less2);
                code().action(Action::Not);
                return true;
            case OperatorType::Equals: code().action(Action::Equals2); return true;
            case OperatorType::NotEquals:
                code().action(Action::Equals2);
                code().action(Action::Not);
                return true;
            case OperatorType::StrictEquals: code().action(Action::StrictEquals); return true;
            case OperatorType::StrictNotEquals:
                code().action(Action::StrictEquals);
                code().action(Action::Not);
                return true;
            case OperatorType::Instanceof: code().action(Action::InstanceOf); return true;
            default:
                fail(line, "this operator is not in ActionScript 2");
        }
    }

    void compileBinary(BinaryExpression* b) {
        if (b->op == OperatorType::LogicalAnd || b->op == OperatorType::LogicalOr) {
            Label end;
            compileExpression(b->left.get());
            code().action(Action::PushDuplicate);
            if (b->op == OperatorType::LogicalAnd) code().action(Action::Not);
            code().jump(Action::If, end);
            code().action(Action::Pop);
            compileExpression(b->right.get());
            code().bind(end);
            return;
        }
        if (b->op == OperatorType::In) {
            // "name" in object: whether it has the name.
            compileExpression(b->right.get());
            compileExpression(b->left.get());
            code().action(Action::GetMember);
            code().push(Value::undefined());
            code().action(Action::StrictEquals);
            code().action(Action::Not);
            return;
        }
        compileExpression(b->left.get());
        compileExpression(b->right.get());
        emitBinary(b->op, b->line);
    }

    void compileUnary(UnaryExpression* u) {
        switch (u->op) {
            case OperatorType::PreIncrement:
            case OperatorType::PreDecrement:
            case OperatorType::PostIncrement:
            case OperatorType::PostDecrement:
                compileIncrement(u->operand.get(),
                                 u->op == OperatorType::PreIncrement || u->op == OperatorType::PostIncrement,
                                 u->op == OperatorType::PostIncrement || u->op == OperatorType::PostDecrement, false,
                                 u->line);
                return;
            case OperatorType::LogicalNot:
                compileExpression(u->operand.get());
                code().action(Action::Not);
                return;
            case OperatorType::Negative:
                code().pushNumber(0);
                compileExpression(u->operand.get());
                code().action(Action::Subtract);
                return;
            case OperatorType::Positive:
                compileExpression(u->operand.get());
                code().action(Action::ToNumber);
                return;
            case OperatorType::BitwiseNot:
                compileExpression(u->operand.get());
                code().pushNumber(-1);
                code().action(Action::BitXor);
                return;
            case OperatorType::Typeof:
                compileExpression(u->operand.get());
                code().action(Action::TypeOf);
                return;
            case OperatorType::Void:
                compileDiscarded(u->operand.get());
                code().push(Value::undefined());
                return;
            case OperatorType::Delete: {
                Expression* target = u->operand.get();
                if (auto* m = dynamic_cast<MemberExpression*>(target)) {
                    compileExpression(m->object.get());
                    pushKey(m);
                    code().action(Action::Delete);
                } else if (auto* id = dynamic_cast<Identifier*>(target)) {
                    code().pushString(id->name);
                    code().action(Action::Delete2);
                } else {
                    compileDiscarded(target);
                    code().push(Value::boolean_(true));
                }
                return;
            }
            default:
                fail(u->line, "this operator is not in ActionScript 2");
        }
    }

    // ++ and --, and what they leave: nothing when discarded, else the new
    // value or, postfix, the old.
    void compileIncrement(Expression* target, bool increment, bool postfix, bool discard, int line) {
        const uint8_t op = increment ? Action::Increment : Action::Decrement;
        if (auto* id = dynamic_cast<Identifier*>(target)) {
            if (ExpressionPtr member = memberFor(id->name)) {
                compileIncrement(member.get(), increment, postfix, discard, line);
                return;
            }
            if (const uint8_t r = registerOf(id->name)) {
                code().pushRegister(r);
                if (!discard && postfix) code().action(Action::PushDuplicate);
                code().action(op);
                code().storeRegister(r);
                if (discard || postfix) code().action(Action::Pop);
                return;
            }
            if (!discard && postfix) {
                loadName(id->name);
                code().storeRegister(0);
                code().pushString(id->name);
                code().pushRegister(0);
                code().action(op);
                code().action(Action::SetVariable);
                return;
            }
            code().pushString(id->name);
            loadName(id->name);
            code().action(op);
            if (!discard) code().storeRegister(0);
            code().action(Action::SetVariable);
            if (!discard) code().pushRegister(0);
            return;
        }
        if (auto* m = dynamic_cast<MemberExpression*>(target)) {
            withMember(m, line, [&](const std::function<void()>& object, const std::function<void()>& key) {
                if (!discard && postfix) {
                    object();
                    key();
                    code().action(Action::GetMember);
                    code().storeRegister(0);
                    object();
                    key();
                    code().pushRegister(0);
                    code().action(op);
                    code().action(Action::SetMember);
                    return;
                }
                object();
                key();
                object();
                key();
                code().action(Action::GetMember);
                code().action(op);
                if (!discard) code().storeRegister(0);
                code().action(Action::SetMember);
                if (!discard) code().pushRegister(0);
            });
            return;
        }
        fail(line, "++ and -- need a variable or a property");
    }

    // Runs \a body with ways to push a member's object and key again: the
    // expressions themselves when simple, else registers holding them.
    void withMember(MemberExpression* m, int line,
                    const std::function<void(const std::function<void()>&, const std::function<void()>&)>& body) {
        uint8_t objectRegister = 0, keyRegister = 0;
        Expression* object = m->object.get();
        if (!simple(object)) {
            objectRegister = allocTemp(line);
            compileExpression(object);
            code().storeRegister(objectRegister);
            code().action(Action::Pop);
        }
        if (m->computed && !simple(m->computedProperty.get())) {
            keyRegister = allocTemp(line);
            compileExpression(m->computedProperty.get());
            code().storeRegister(keyRegister);
            code().action(Action::Pop);
        }
        const auto pushObject = [&]() {
            if (objectRegister) {
                code().pushRegister(objectRegister);
            } else {
                compileExpression(object);
            }
        };
        const auto pushTheKey = [&]() {
            if (keyRegister) {
                code().pushRegister(keyRegister);
            } else {
                pushKey(m);
            }
        };
        body(pushObject, pushTheKey);
        if (keyRegister) freeTemp(keyRegister);
        if (objectRegister) freeTemp(objectRegister);
    }

    void compileAssignment(AssignmentExpression* a, bool discard) {
        compileAssign(a->op, a->left.get(), a->right.get(), discard, a->line);
    }

    void compileAssign(OperatorType op, Expression* target, Expression* right, bool discard, int line) {
        if (op == OperatorType::LogicalAnd || op == OperatorType::LogicalOr || op == OperatorType::NullCoalescing ||
            op == OperatorType::Power) {
            fail(line, "this assignment is not in ActionScript 2");
        }
        const bool compound = op != OperatorType::Assign;
        if (auto* id = dynamic_cast<Identifier*>(target)) {
            if (ExpressionPtr member = memberFor(id->name)) {
                compileAssign(op, member.get(), right, discard, line);
                return;
            }
            if (const uint8_t r = registerOf(id->name)) {
                if (compound) code().pushRegister(r);
                compileExpression(right);
                if (compound) emitBinary(op, line);
                code().storeRegister(r);
                if (discard) code().action(Action::Pop);
                return;
            }
            code().pushString(id->name);
            if (compound) loadName(id->name);
            compileExpression(right);
            if (compound) emitBinary(op, line);
            if (!discard) code().storeRegister(0);
            code().action(Action::SetVariable);
            if (!discard) code().pushRegister(0);
            return;
        }
        if (auto* m = dynamic_cast<MemberExpression*>(target)) {
            const auto assign = [&](const std::function<void()>& object, const std::function<void()>& key) {
                object();
                key();
                if (compound) {
                    object();
                    key();
                    code().action(Action::GetMember);
                }
                compileExpression(right);
                if (compound) emitBinary(op, line);
                if (!discard) code().storeRegister(0);
                code().action(Action::SetMember);
                if (!discard) code().pushRegister(0);
            };
            if (!compound) {
                // Each part evaluated once anyway.
                assign([&]() { compileExpression(m->object.get()); }, [&]() { pushKey(m); });
            } else {
                withMember(m, line, assign);
            }
            return;
        }
        fail(line, "that cannot be assigned to");
    }

    // Arguments pushed last first, then how many.
    void pushArguments(const ExpressionList& args, size_t from = 0) {
        for (size_t i = args.size(); i-- > from;) {
            compileExpression(args[i].get());
        }
        code().pushNumber(static_cast<double>(args.size() - std::min(from, args.size())));
    }

    // A call. Returns true when \a discard was honored and nothing was left
    // on the stack (the actions that give no value).
    bool compileCall(CallExpression* call, bool discard) {
        Expression* callee = call->callee.get();
        const int line = call->line;
        if (auto* id = dynamic_cast<Identifier*>(callee)) {
            // A class's method by its bare name: on this, or on the class.
            if (ExpressionPtr member = memberFor(id->name)) {
                auto* m = static_cast<MemberExpression*>(member.get());
                pushArguments(call->arguments);
                compileExpression(m->object.get());
                pushKey(m);
                code().action(Action::CallMethod);
                return false;
            }
            if (!registerOf(id->name)) {
                int result = globalFunction(id->name, call->arguments, line);
                if (result >= 0) {
                    // 0: an action that leaves nothing; 1: one that leaves a
                    // value.
                    if (result == 0 && !discard) code().push(Value::undefined());
                    if (result == 1 && discard) code().action(Action::Pop);
                    return discard;
                }
            }
            pushArguments(call->arguments);
            if (const uint8_t r = registerOf(id->name)) {
                code().pushRegister(r);
                code().push(Value::undefined());
                code().action(Action::CallMethod);
            } else {
                const std::string path = qualify(id->name);
                if (path != id->name) {
                    const size_t dot = path.find_last_of('.');
                    pushPath(path.substr(0, dot));
                    code().pushString(path.substr(dot + 1));
                    code().action(Action::CallMethod);
                } else {
                    code().pushString(id->name);
                    code().action(Action::CallFunction);
                }
            }
            return false;
        }
        if (dynamic_cast<SuperExpression*>(callee)) {
            // super(...): the class extended, on this.
            pushArguments(call->arguments);
            loadSuper(line);
            code().push(Value::undefined());
            code().action(Action::CallMethod);
            return false;
        }
        if (auto* m = dynamic_cast<MemberExpression*>(callee)) {
            pushArguments(call->arguments);
            std::string path;
            if (qualifiedPath(m, path)) {
                const size_t dot = path.find_last_of('.');
                pushPath(path.substr(0, dot));
                code().pushString(path.substr(dot + 1));
            } else {
                compileExpression(m->object.get());
                pushKey(m);
            }
            code().action(Action::CallMethod);
            return false;
        }
        pushArguments(call->arguments);
        compileExpression(callee);
        code().push(Value::undefined());
        code().action(Action::CallMethod);
        return false;
    }

    void compileNew(NewExpression* n) {
        Expression* callee = n->callee.get();
        pushArguments(n->arguments);
        if (auto* id = dynamic_cast<Identifier*>(callee)) {
            if (const uint8_t r = registerOf(id->name)) {
                code().pushRegister(r);
                code().push(Value::undefined());
                code().action(Action::NewMethod);
                return;
            }
            const std::string path = qualify(id->name);
            if (path != id->name) {
                const size_t dot = path.find_last_of('.');
                pushPath(path.substr(0, dot));
                code().pushString(path.substr(dot + 1));
                code().action(Action::NewMethod);
                return;
            }
            code().pushString(id->name);
            code().action(Action::NewObject);
            return;
        }
        if (auto* m = dynamic_cast<MemberExpression*>(callee)) {
            std::string path;
            if (qualifiedPath(m, path)) {
                const size_t dot = path.find_last_of('.');
                pushPath(path.substr(0, dot));
                code().pushString(path.substr(dot + 1));
            } else {
                compileExpression(m->object.get());
                pushKey(m);
            }
            code().action(Action::NewMethod);
            return;
        }
        compileExpression(callee);
        code().push(Value::undefined());
        code().action(Action::NewMethod);
    }

    // Literal text, or null when the expression is not a string literal.
    static const std::string* stringLiteral(Expression* expr) {
        auto* literal = dynamic_cast<Literal*>(expr);
        return literal && literal->kind == Literal::LiteralKind::String ? &literal->stringValue : nullptr;
    }

    static bool numberLiteral(Expression* expr, double& value) {
        auto* literal = dynamic_cast<Literal*>(expr);
        if (!literal || literal->kind != Literal::LiteralKind::Numeric) return false;
        value = literal->numberValue;
        return true;
    }

    void needArguments(const ExpressionList& args, size_t least, size_t most, const std::string& name, int line) {
        if (args.size() < least || args.size() > most) {
            fail(line, name + "() takes " + (least == most ? std::to_string(least) : std::to_string(least) + " to " +
                                                                                      std::to_string(most)) +
                           " argument" + (most == 1 ? "" : "s"));
        }
    }

    // The frame a goto is on: a number (from 1) or a label; with a scene
    // first, on the main timeline. Leaves the action's play flag set.
    void compileGoto(const ExpressionList& args, bool play, int line, const std::string& name) {
        needArguments(args, 1, 2, name, line);
        Expression* frame = args.back().get();
        const bool main = _options.sceneStart >= 0;
        int bias = main ? _options.sceneStart : 0;
        if (args.size() == 2) {
            const std::string* scene = stringLiteral(args[0].get());
            if (!scene) fail(line, name + "() needs a scene's name as written, in quotes");
            const auto found = _options.scenes.find(*scene);
            if (found == _options.scenes.end()) fail(line, "there is no scene named \"" + *scene + "\"");
            bias = found->second;
        }
        double number = 0;
        if (numberLiteral(frame, number)) {
            const int index = std::max(0, static_cast<int>(number) - 1 + bias);
            std::vector<uint8_t> payload;
            appendU16(payload, static_cast<size_t>(std::min(index, 65535)));
            code().record(Action::GotoFrame, payload);
            if (play) code().action(Action::Play);
            return;
        }
        if (const std::string* label = stringLiteral(frame)) {
            std::vector<uint8_t> payload;
            appendString(payload, *label);
            code().record(Action::GotoLabel, payload);
            if (play) code().action(Action::Play);
            return;
        }
        compileExpression(frame);
        std::vector<uint8_t> payload;
        payload.push_back(static_cast<uint8_t>((bias > 0 ? 0x02 : 0x00) | (play ? 0x01 : 0x00)));
        if (bias > 0) appendU16(payload, static_cast<size_t>(std::min(bias, 65535)));
        code().record(Action::GotoFrame2, payload);
    }

    // "GET" and "POST" as GetURL2 numbers them.
    int methodOf(const ExpressionList& args, size_t at, int line) {
        if (args.size() <= at) return 0;
        const std::string* method = stringLiteral(args[at].get());
        if (!method) fail(line, "the method is \"GET\" or \"POST\", in quotes");
        std::string upper = *method;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
        return upper == "GET" ? 1 : upper == "POST" ? 2 : 0;
    }

    // A level's target: "_level" and its number.
    void pushLevel(Expression* level) {
        double number = 0;
        if (numberLiteral(level, number)) {
            code().pushString("_level" + numberText(number));
            return;
        }
        code().pushString("_level");
        compileExpression(level);
        code().action(Action::Add2);
    }

    void getURL2(int flags) { code().record(Action::GetURL2, {static_cast<uint8_t>(flags)}); }

    // The global functions that are actions of their own. -1 when \a name
    // is not one; else 0 when the action leaves nothing, 1 when it leaves
    // the value.
    int globalFunction(const std::string& name, const ExpressionList& args, int line) {
        const auto simpleAction = [&](uint8_t action, size_t count, int leaves) {
            needArguments(args, count, count, name, line);
            for (size_t i = 0; i < count; ++i) compileExpression(args[i].get());
            code().action(action);
            return leaves;
        };
        if (name == "play") return simpleAction(Action::Play, 0, 0);
        if (name == "stop") return simpleAction(Action::Stop, 0, 0);
        if (name == "nextFrame") return simpleAction(Action::NextFrame, 0, 0);
        if (name == "prevFrame") return simpleAction(Action::PrevFrame, 0, 0);
        if (name == "stopAllSounds") return simpleAction(Action::StopSounds, 0, 0);
        if (name == "toggleHighQuality") return simpleAction(Action::ToggleQuality, 0, 0);
        if (name == "stopDrag") return simpleAction(Action::EndDrag, 0, 0);
        if (name == "trace") return simpleAction(Action::Trace, 1, 0);
        if (name == "getTimer") return simpleAction(Action::GetTime, 0, 1);
        if (name == "random") return simpleAction(Action::RandomNumber, 1, 1);
        if (name == "int") return simpleAction(Action::ToInteger, 1, 1);
        if (name == "chr") return simpleAction(Action::AsciiToChar, 1, 1);
        if (name == "ord") return simpleAction(Action::CharToAscii, 1, 1);
        if (name == "mbchr") return simpleAction(Action::MBAsciiToChar, 1, 1);
        if (name == "mbord") return simpleAction(Action::MBCharToAscii, 1, 1);
        if (name == "length" && args.size() == 1) return simpleAction(Action::StringLength, 1, 1);
        if (name == "mblength") return simpleAction(Action::MBStringLength, 1, 1);
        if (name == "substring") return simpleAction(Action::StringExtract, 3, 1);
        if (name == "mbsubstring") return simpleAction(Action::MBStringExtract, 3, 1);
        if (name == "targetPath") return simpleAction(Action::TargetPath, 1, 1);
        if (name == "eval") return simpleAction(Action::GetVariable, 1, 1);
        if (name == "removeMovieClip") return simpleAction(Action::RemoveSprite, 1, 0);
        if (name == "call") return simpleAction(Action::Call, 1, 0);
        if (name == "set") return simpleAction(Action::SetVariable, 2, 0);
        if (name == "Number" && args.size() == 1) return simpleAction(Action::ToNumber, 1, 1);
        if (name == "String" && args.size() == 1) return simpleAction(Action::ToString, 1, 1);
        if (name == "gotoAndPlay" || name == "gotoAndStop") {
            compileGoto(args, name == "gotoAndPlay", line, name);
            return 0;
        }
        if (name == "nextScene" || name == "prevScene") {
            needArguments(args, 0, 0, name, line);
            const int start = name == "nextScene" ? _options.nextScene : _options.prevScene;
            if (start < 0) {
                // No scene that way: nothing happens.
                return 0;
            }
            std::vector<uint8_t> payload;
            appendU16(payload, static_cast<size_t>(std::min(start, 65535)));
            code().record(Action::GotoFrame, payload);
            code().action(Action::Play);
            return 0;
        }
        if (name == "getProperty") {
            needArguments(args, 2, 2, name, line);
            auto* property = dynamic_cast<Identifier*>(args[1].get());
            const int index = property ? propertyIndex(property->name) : -1;
            if (index < 0) fail(line, "getProperty() needs a property such as _x");
            compileExpression(args[0].get());
            code().pushNumber(index);
            code().action(Action::GetProperty);
            return 1;
        }
        if (name == "setProperty") {
            needArguments(args, 3, 3, name, line);
            auto* property = dynamic_cast<Identifier*>(args[1].get());
            const int index = property ? propertyIndex(property->name) : -1;
            if (index < 0) fail(line, "setProperty() needs a property such as _x");
            compileExpression(args[0].get());
            code().pushNumber(index);
            compileExpression(args[2].get());
            code().action(Action::SetProperty);
            return 0;
        }
        if (name == "duplicateMovieClip") {
            needArguments(args, 3, 3, name, line);
            compileExpression(args[0].get());
            compileExpression(args[1].get());
            double depth = 0;
            // Clips made by scripts go above the timeline's, from 16384.
            if (numberLiteral(args[2].get(), depth)) {
                code().pushNumber(depth + 16384);
            } else {
                compileExpression(args[2].get());
                code().pushNumber(16384);
                code().action(Action::Add2);
            }
            code().action(Action::CloneSprite);
            return 0;
        }
        if (name == "startDrag") {
            needArguments(args, 1, 6, name, line);
            if (args.size() != 1 && args.size() != 2 && args.size() != 6) {
                fail(line, "startDrag() takes a target, whether to lock the center, and the four sides of a box");
            }
            if (args.size() == 6) {
                for (size_t i = 2; i < 6; ++i) compileExpression(args[i].get());
                code().pushNumber(1);
            } else {
                code().pushNumber(0);
            }
            if (args.size() >= 2) {
                compileExpression(args[1].get());
            } else {
                code().pushNumber(0);
            }
            compileExpression(args[0].get());
            code().action(Action::StartDrag);
            return 0;
        }
        if (name == "getURL") {
            needArguments(args, 1, 3, name, line);
            const std::string* url = stringLiteral(args[0].get());
            const std::string* window = args.size() >= 2 ? stringLiteral(args[1].get()) : nullptr;
            if (url && args.size() <= 2 && (args.size() == 1 || window)) {
                std::vector<uint8_t> payload;
                appendString(payload, *url);
                appendString(payload, window ? *window : std::string());
                code().record(Action::GetURL, payload);
                return 0;
            }
            compileExpression(args[0].get());
            if (args.size() >= 2) {
                compileExpression(args[1].get());
            } else {
                code().pushString("");
            }
            getURL2(methodOf(args, 2, line) << 6);
            return 0;
        }
        if (name == "fscommand") {
            needArguments(args, 1, 2, name, line);
            const std::string* command = stringLiteral(args[0].get());
            const std::string* argument = args.size() == 2 ? stringLiteral(args[1].get()) : nullptr;
            if (command && (args.size() == 1 || argument)) {
                std::vector<uint8_t> payload;
                appendString(payload, "FSCommand:" + *command);
                appendString(payload, argument ? *argument : std::string());
                code().record(Action::GetURL, payload);
                return 0;
            }
            code().pushString("FSCommand:");
            compileExpression(args[0].get());
            code().action(Action::StringAdd);
            if (args.size() == 2) {
                compileExpression(args[1].get());
            } else {
                code().pushString("");
            }
            getURL2(0);
            return 0;
        }
        if (name == "loadMovie" || name == "loadVariables") {
            needArguments(args, 2, 3, name, line);
            compileExpression(args[0].get());
            compileExpression(args[1].get());
            getURL2((methodOf(args, 2, line) << 6) | 0x02 | (name == "loadVariables" ? 0x01 : 0x00));
            return 0;
        }
        if (name == "loadMovieNum" || name == "loadVariablesNum") {
            needArguments(args, 2, 3, name, line);
            compileExpression(args[0].get());
            pushLevel(args[1].get());
            getURL2((methodOf(args, 2, line) << 6) | (name == "loadVariablesNum" ? 0x01 : 0x00));
            return 0;
        }
        if (name == "unloadMovie") {
            needArguments(args, 1, 1, name, line);
            code().pushString("");
            compileExpression(args[0].get());
            getURL2(0x02);
            return 0;
        }
        if (name == "unloadMovieNum") {
            needArguments(args, 1, 1, name, line);
            code().pushString("");
            pushLevel(args[0].get());
            getURL2(0x00);
            return 0;
        }
        if (name == "ifFrameLoaded") {
            // Whether the frame (from 1) is loaded: as many frames loaded.
            needArguments(args, 1, 2, name, line);
            code().pushString("_framesloaded");
            code().action(Action::GetVariable);
            compileExpression(args.back().get());
            code().action(Action::Less2);
            code().action(Action::Not);
            return 1;
        }
        if (name == "__tellTarget") {
            needArguments(args, 2, 2, "tellTarget", line);
            auto* block = dynamic_cast<FunctionExpression*>(args[1].get());
            if (!block) fail(line, "tellTarget needs a block");
            if (const std::string* target = stringLiteral(args[0].get())) {
                std::vector<uint8_t> payload;
                appendString(payload, *target);
                code().record(Action::SetTarget, payload);
            } else {
                compileExpression(args[0].get());
                code().action(Action::SetTarget2);
            }
            compileStatement(block->body.get());
            std::vector<uint8_t> payload;
            appendString(payload, "");
            code().record(Action::SetTarget, payload);
            return 0;
        }
        if (name == "__on" || name == "__onClipEvent") {
            fail(line, std::string(name == "__on" ? "on()" : "onClipEvent()") +
                           " goes on a button or a movie clip instance, not a frame");
        }
        return -1;
    }
};

// The parts of a script between #initclip and #endinitclip, and the rest;
// lines kept where they were, so errors give the line written.
void splitInitClip(const std::string& source, std::string& frame, std::string& init) {
    frame.clear();
    init.clear();
    bool inInit = false;
    size_t start = 0;
    while (start <= source.size()) {
        size_t end = source.find('\n', start);
        const bool last = end == std::string::npos;
        if (last) end = source.size();
        const std::string line = source.substr(start, end - start);
        size_t first = line.find_first_not_of(" \t\r");
        const std::string word = first == std::string::npos ? std::string() : line.substr(first);
        if (word.compare(0, 9, "#initclip") == 0) {
            inInit = true;
        } else if (word.compare(0, 12, "#endinitclip") == 0) {
            inInit = false;
        } else {
            (inInit ? init : frame) += line;
        }
        if (last) break;
        frame += '\n';
        init += '\n';
        start = end + 1;
    }
}

bool blank(const std::string& text) { return text.find_first_not_of(" \t\r\n") == std::string::npos; }

} // namespace

Compiler::Compiler(const Options& options) : _options(options) {}

std::unique_ptr<Program> Compiler::parse(const std::string& source, const std::string& fileName) {
    Parser parser(source, fileName);
    std::unique_ptr<Program> program = parser.parse();
    if (parser.hasErrors()) {
        std::istringstream lines(parser.getErrors());
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty()) _errors.push_back((fileName.empty() ? std::string() : fileName + ", ") + line);
        }
        return nullptr;
    }
    return program;
}

std::vector<uint8_t> Compiler::compileScript(const std::string& source, const std::string& fileName) {
    _errors.clear();
    std::unique_ptr<Program> program = parse(source, fileName);
    if (!program) return {};
    std::vector<Statement*> statements;
    std::vector<ImportDirective*> imports;
    for (auto& import : program->imports) imports.push_back(import.get());
    for (auto& node : program->statements) {
        if (auto* statement = dynamic_cast<Statement*>(node.get())) {
            statements.push_back(statement);
        } else if (auto* import = dynamic_cast<ImportDirective*>(node.get())) {
            imports.push_back(import);
        }
    }
    Generator generator(_options, _errors, fileName);
    generator.setImports(imports);
    try {
        std::vector<uint8_t> out = generator.script(statements);
        if (!_errors.empty()) return {};
        return out;
    } catch (const Abandon&) {
        return {};
    }
}

FrameScript Compiler::compileFrameScript(const std::string& source, const std::string& fileName) {
    FrameScript out;
    std::string frame, init;
    splitInitClip(source, frame, init);
    std::vector<std::string> errors;
    if (!blank(init)) {
        out.initActions = compileScript(init, fileName);
        errors = _errors;
    }
    if (!blank(frame)) {
        out.actions = compileScript(frame, fileName);
        errors.insert(errors.end(), _errors.begin(), _errors.end());
    }
    _errors = errors;
    if (!_errors.empty()) return FrameScript();
    return out;
}

std::vector<Handler> Compiler::compileHandlers(const std::string& source, const std::string& fileName) {
    _errors.clear();
    std::unique_ptr<Program> program = parse(source, fileName);
    if (!program) return {};
    std::vector<Handler> handlers;
    const std::string where = fileName.empty() ? std::string() : fileName + ", ";
    for (auto& node : program->statements) {
        auto* statement = dynamic_cast<ExpressionStatement*>(node.get());
        auto* call = statement ? dynamic_cast<CallExpression*>(statement->expression.get()) : nullptr;
        auto* callee = call ? dynamic_cast<Identifier*>(call->callee.get()) : nullptr;
        if (dynamic_cast<EmptyStatement*>(node.get())) continue;
        if (!callee || (callee->name != "__on" && callee->name != "__onClipEvent") || call->arguments.size() != 2) {
            const int line = statement ? statement->line : 0;
            _errors.push_back(where + "line " + std::to_string(line) +
                              ": an instance's script has only on() and onClipEvent() handlers");
            return {};
        }
        Handler handler;
        handler.clipEvent = callee->name == "__onClipEvent";
        handler.line = statement->line;
        if (auto* events = dynamic_cast<ArrayExpression*>(call->arguments[0].get())) {
            for (auto& event : events->elements) {
                if (auto* literal = dynamic_cast<Literal*>(event.get())) handler.events.push_back(literal->stringValue);
            }
        }
        auto* block = dynamic_cast<FunctionExpression*>(call->arguments[1].get());
        auto* body = block ? dynamic_cast<BlockStatement*>(block->body.get()) : nullptr;
        std::vector<Statement*> statements;
        if (body) {
            for (auto& s : body->statements) statements.push_back(s.get());
        }
        Generator generator(_options, _errors, fileName);
        try {
            handler.actions = generator.script(statements);
        } catch (const Abandon&) {
            return {};
        }
        if (!_errors.empty()) return {};
        handlers.push_back(std::move(handler));
    }
    return handlers;
}

bool Compiler::classMembers(const std::string& source, std::string& name, ClassMembers& out,
                            const std::string& fileName) {
    _errors.clear();
    out = ClassMembers();
    name.clear();
    std::unique_ptr<Program> program = parse(source, fileName);
    if (!program) return false;
    for (auto& node : program->statements) {
        if (auto* definition = dynamic_cast<ClassDefinition*>(node.get())) {
            name = definition->name ? definition->name->name : std::string();
            out = membersOf(definition);
            // Its superclass by its qualified name, as its imports give it.
            if (!out.superName.empty() && out.superName.find('.') == std::string::npos) {
                const auto qualifiedBy = [&](const ImportDirective* import) {
                    return import && import->name && import->name->name == out.superName && !import->path.empty();
                };
                for (auto& import : program->imports) {
                    if (qualifiedBy(import.get())) out.superName = import->path + "." + out.superName;
                }
                for (auto& other : program->statements) {
                    auto* import = dynamic_cast<ImportDirective*>(other.get());
                    if (qualifiedBy(import)) out.superName = import->path + "." + out.superName;
                }
            }
            return true;
        }
    }
    return false;
}

bool Compiler::compileClass(const std::string& source, ClassCode& out, const std::string& fileName) {
    _errors.clear();
    out = ClassCode();
    std::unique_ptr<Program> program = parse(source, fileName);
    if (!program) return false;
    ClassDefinition* definition = nullptr;
    std::vector<ImportDirective*> imports;
    for (auto& import : program->imports) imports.push_back(import.get());
    for (auto& node : program->statements) {
        if (auto* cd = dynamic_cast<ClassDefinition*>(node.get())) {
            if (definition) {
                _errors.push_back((fileName.empty() ? std::string() : fileName + ", ") +
                                  "a class file has one class");
                return false;
            }
            definition = cd;
        } else if (auto* import = dynamic_cast<ImportDirective*>(node.get())) {
            imports.push_back(import);
        }
    }
    if (!definition) {
        _errors.push_back((fileName.empty() ? std::string() : fileName + ", ") + "the file has no class");
        return false;
    }
    Generator generator(_options, _errors, fileName);
    try {
        out.actions = generator.classScript(definition, imports, out);
    } catch (const Abandon&) {
        out = ClassCode();
        return false;
    }
    if (!_errors.empty()) {
        out = ClassCode();
        return false;
    }
    return true;
}

} // namespace avm1
} // namespace as3
