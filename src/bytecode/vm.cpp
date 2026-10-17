#include "vm.h"
#include "opcode.h"
#include "compiler.h"
#include "../parser/parser.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace as3 {

namespace {

constexpr size_t kStackSize = 1 << 17;
constexpr size_t kMaxFrames = 4000;
constexpr int kMaxNativeDepth = 2000;

enum FrameMode : uint8_t {
    kModeNormal = 0,
    // A constructor or a setter: what was saved, whatever is returned.
    kModeSaved = 1,
    // super(...): nothing.
    kModeDiscard = 2,
    // new on a plain function: what it returns if an object, else what was
    // saved.
    kModeConstructFunction = 3,
};

double nowMilliseconds() {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()) / 1000.0;
}

} // namespace

VM::VM()
    : parent(nullptr)
    , frames(kMaxFrames + 16) {
    stackBase = static_cast<Val*>(::operator new(kStackSize * sizeof(Val)));
    stackEnd = stackBase + kStackSize;
    stackBuilt = stackBase;
    sp = stackBase;
    growStack(stackBase + 1024);
    started = nowMilliseconds();
    setupBuiltins();
}

VM::VM(VM* parentVM)
    : parent(parentVM)
    , frames(kMaxFrames + 16) {
    stackBase = static_cast<Val*>(::operator new(kStackSize * sizeof(Val)));
    stackEnd = stackBase + kStackSize;
    stackBuilt = stackBase;
    sp = stackBase;
    growStack(stackBase + 1024);
    started = nowMilliseconds();
    // The parent's built-in classes, which its globals hold.
    if (parent) {
        core = parent->core;
        classResolver = parent->classResolver;
    }
}

VM::~VM() {
    releaseObjects();
    while (frames.size() > 0) {
        frames.pop_back();
    }
    for (Val* p = stackBase; p < stackBuilt; ++p) {
        p->~Val();
    }
    ::operator delete(stackBase);
}

void VM::growStack(Val* needed) {
    if (needed <= stackBuilt) {
        return;
    }
    if (needed > stackEnd) {
        throwError("Error", "Error #1023: Stack overflow occurred.", 1023);
    }
    // In steps, so that it is not grown at every call.
    Val* end = std::min(stackEnd, std::max(needed, stackBuilt + 4096));
    for (; stackBuilt < end; ++stackBuilt) {
        new (stackBuilt) Val();
    }
}

double VM::now() {
    if (clock) {
        return clock();
    }
    return nowMilliseconds() - started;
}

void VM::track(const ValuePtr& value) {
    made.push_back(value);
    if (made.size() >= madeCheck) {
        made.erase(std::remove_if(made.begin(), made.end(), [](const std::weak_ptr<Value>& v) { return v.expired(); }),
                   made.end());
        madeCheck = std::max<size_t>(1024, made.size() * 2);
    }
}

void VM::releaseObjects() {
    std::vector<ValuePtr> alive;
    alive.reserve(made.size());
    for (auto& weak : made) {
        if (ValuePtr value = weak.lock()) {
            alive.push_back(std::move(value));
        }
    }
    made.clear();
    for (const ValuePtr& value : alive) {
        value->properties.clear();
        value->elements.clear();
        value->slots.clear();
        value->boundMethods.reset();
        if (value->function) {
            value->function->captured.clear();
            value->function->boundThis = Val();
            value->function->scopeThis = Val();
            value->function->scope = Val();
        }
        if (value->type == ValueType::Class && value->cls) {
            value->cls->staticSlots.clear();
            value->cls->statics.clear();
            value->cls->traits.clear();
            value->cls->prototype.clear();
            value->cls->constructor = Val();
            value->cls->slotDefaults.clear();
        }
    }
    timers.clear();
    for (Val* p = stackBase; p < stackBuilt; ++p) {
        *p = Val();
    }
    errorValue = Val();
}

void VM::clearError() {
    hadError = false;
    errorMessage.clear();
    errorLine = -1;
    errorValue = Val();
    errorStack.clear();
}

int VM::currentLine() {
    if (frames.empty()) {
        return -1;
    }
    const CallFrame& frame = frames.back();
    if (!frame.code || !frame.ip) {
        return -1;
    }
    const size_t offset = static_cast<size_t>(frame.ip - frame.codeStart);
    return frame.code->chunk.getLine(offset > 0 ? offset - 1 : 0);
}

void VM::reportError(const ScriptException& exception) {
    hadError = true;
    errorValue = exception.value;
    errorLine = exception.line;
    try {
        errorMessage = toString(exception.value);
        if (exception.value.isObject() && exception.value->cls && exception.value->cls->isSubclassOf(core.error.get())) {
            errorStack = getProperty(exception.value, "$stack").toString();
        }
    } catch (...) {
        errorMessage = exception.value.toString();
    }
    if (errorMessage.empty()) {
        errorMessage = "Error";
    }
}

// Running

void VM::execute(std::shared_ptr<BytecodeFunction> function, ValuePtr scopeValue) {
    clearError();
    if (!function) {
        return;
    }
    Val scope(scopeValue);
    Val code = makeClosure(function, nullptr);
    code->function->scope = scope;
    code->function->scopeThis = scope;
    try {
        runCall(code, scope, Args(), scope);
    } catch (ScriptException& exception) {
        reportError(exception);
    } catch (const std::exception& failed) {
        hadError = true;
        errorMessage = failed.what();
    }
}

bool VM::load(const std::string& source, const std::string& fileName, ValuePtr scope) {
    clearError();
    Parser parser(source, fileName);
    auto program = parser.parse();
    if (!parser.getWarnings().empty()) {
        warnings.push_back(parser.getWarnings());
    }
    if (parser.hasErrors()) {
        hadError = true;
        errorMessage = parser.getErrors();
        const size_t at = errorMessage.find("at line ");
        errorLine = at == std::string::npos ? -1 : std::atoi(errorMessage.c_str() + at + 8);
        return false;
    }
    std::shared_ptr<BytecodeFunction> code;
    try {
        Compiler compiler;
        code = compiler.compile(program.get());
        for (const auto& warning : compiler.getWarnings()) {
            warnings.push_back(warning);
        }
    } catch (const std::exception& failed) {
        hadError = true;
        errorMessage = failed.what();
        return false;
    }
    execute(code, scope);
    return !hadError;
}

ValuePtr VM::call(ValuePtr function, const std::vector<ValuePtr>& args, ValuePtr thisValue, ValuePtr scopeValue) {
    clearError();
    Val fn(function);
    if (!fn.isFunction() && !fn.isClass()) {
        hadError = true;
        errorMessage = "Can only call functions";
        return Value::makeUndefined();
    }
    std::vector<Val> values(args.begin(), args.end());
    try {
        return runCall(fn, Val(thisValue), Args(values.data(), values.size()), Val(scopeValue));
    } catch (ScriptException& exception) {
        reportError(exception);
    } catch (const std::exception& failed) {
        hadError = true;
        errorMessage = failed.what();
    }
    return Value::makeUndefined();
}

void VM::enterNative(const char* marker) {
    // The C++ stack a nested run uses: measured from where the host first
    // called in, and stopped short of the thread's end.
    if (nativeDepth == 0) {
        stackEntry = marker;
    } else if (!overflowing &&
               ((stackEntry && static_cast<size_t>(stackEntry > marker ? stackEntry - marker : marker - stackEntry) >
                                   nativeStackLimit) ||
                nativeDepth >= kMaxNativeDepth)) {
        // Making the error takes a little more: allowed, once.
        overflowing = true;
        ScriptException exception;
        try {
            exception.value = makeError("Error", "Error #1023: Stack overflow occurred.", 1023);
        } catch (...) {
            exception.value = Val::string("Error #1023: Stack overflow occurred.");
        }
        overflowing = false;
        throw exception;
    }
    ++nativeDepth;
}

Val VM::runCall(const Val& function, const Val& thisValue, Args args, const Val& scope) {
    growStack(sp + args.size() + 2);
    char marker = 0;
    enterNative(&marker);
    struct Depth {
        int& depth;
        ~Depth() { --depth; }
    } depth{nativeDepth};
    Val* result = sp++;
    *result = Val();
    Val* argStart = sp;
    for (const Val& arg : args) {
        *sp++ = arg;
    }
    const size_t depthBefore = frames.size();
    // On an exception, what this call put on the stacks is taken off
    // (by a guard, not a catch and rethrow: MSVC runs a catch on top of
    // the frames it catches from, so rethrowing from many nested calls
    // would use the stack up).
    struct Unwind {
        VM& vm;
        size_t depth;
        Val* result;
        bool done = false;
        ~Unwind() {
            if (!done) vm.unwindTo(depth, result);
        }
    } unwind{*this, depthBefore, result};
    if (invoke(function, thisValue, argStart, static_cast<uint32_t>(args.size()), result)) {
        if (!scope.isUndefined()) {
            CallFrame& called = frames.back();
            called.scope = scope;
            if (!called.methodThis && called.scopeThis.isNullish()) {
                called.scopeThis = scope;
            }
        }
        run(depthBefore);
    }
    unwind.done = true;
    Val value = std::move(*result);
    for (Val* p = result; p < sp; ++p) {
        *p = Val();
    }
    sp = result;
    return value;
}

void VM::unwindTo(size_t depth, Val* result) {
    while (frames.size() > depth) {
        frames.pop_back();
    }
    for (Val* p = result; p < sp; ++p) {
        *p = Val();
    }
    sp = result;
}

Val VM::callFunction(const Val& function, const Val& thisValue, Args args) {
    return runCall(function, thisValue, args, Val());
}

bool VM::invoke(const Val& function, const Val& thisValue, Val* args, uint32_t argc, Val* result, uint8_t mode,
                const Val& saved, bool borrowed) {
    if (function.tag == Val::Tag::Object) {
        Value* fn = function.ref.get();
        if (fn->function && fn->function->code) {
            pushFrame(function, *fn->function, thisValue, args, argc, result, mode, saved, borrowed);
            return true;
        }
        if (fn->type == ValueType::Class) {
            Val made = callAsFunction(fn->cls.get(), function, Args(args, argc));
            for (Val* p = result; p < sp; ++p) *p = Val();
            *result = std::move(made);
            sp = result + 1;
            return false;
        }
        if (fn->function || fn->nativeFunc) {
            Val made = callNative(function, thisValue, args, argc);
            if (mode == kModeSaved) {
                made = saved;
            } else if (mode == kModeDiscard) {
                made = Val();
            }
            for (Val* p = result; p < sp; ++p) *p = Val();
            *result = std::move(made);
            sp = result + 1;
            return false;
        }
    }
    throwError("TypeError", "Error #1006: value is not a function.", 1006);
}

Val VM::callNative(const Val& function, const Val& thisValue, Val* args, uint32_t argc) {
    Value* fn = function.ref.get();
    if (fn->function && fn->function->native) {
        const FunctionInfo& info = *fn->function;
        return info.native(*this, info.bound ? info.boundThis : thisValue, Args(args, argc));
    }
    // The simple way: boxed arguments, no 'this'.
    std::vector<ValuePtr> boxed;
    boxed.reserve(argc);
    for (uint32_t i = 0; i < argc; ++i) {
        boxed.push_back(args[i]);
    }
    NativeFunction native = fn->nativeFunc;
    ValuePtr made = native(boxed);
    return Val(made);
}

void VM::pushFrame(const Val& function, FunctionInfo& info, const Val& thisValue, Val* args, uint32_t argc,
                   Val* result, uint8_t mode, const Val& saved, bool borrowed) {
    BytecodeFunction* code = info.code.get();
    if (frames.size() >= kMaxFrames) {
        throwError("Error", "Error #1023: Stack overflow occurred.", 1023);
    }
    const uint32_t numLocals = code->numLocals;
    Val* needed = args + std::max<uint32_t>(argc, numLocals) + code->maxStack + 16;
    if (needed >= stackBuilt) {
        growStack(needed);
    }
    const uint32_t arity = static_cast<uint32_t>(code->arity);

    // The rest parameter and 'arguments' take what was given first.
    Val rest;
    Val arguments;
    if (code->restLocal >= 0 || code->restCell >= 0) {
        std::vector<Val> extra;
        for (uint32_t i = arity; i < argc; ++i) extra.push_back(args[i]);
        rest = newArray(std::move(extra));
    }
    if (code->argumentsLocal >= 0 || code->argumentsCell >= 0) {
        arguments = newArray(std::vector<Val>(args, args + argc));
        arguments->properties["callee"] = function;
    }

    CallFrame& frame = frames.push();
    frame.code = code;
    frame.closure = function.ref.get();
    if (!borrowed) {
        frame.closureHold = function.ref;
    }
    frame.methodThis = false;
    frame.codeStart = code->chunk.getCode().data();
    frame.ip = frame.codeStart;
    frame.base = args;
    frame.result = result;
    frame.argc = argc;
    frame.mode = mode;
    frame.saved = saved;
    frame.thisVal = info.bound ? info.boundThis : thisValue;
    frame.owner = info.owner.get();
    if (code->isMethod) {
        frame.methodThis = true;
    } else if (!info.scopeThis.isUndefined()) {
        frame.scopeThis = info.scopeThis;
    }
    if (!info.scope.isUndefined()) {
        frame.scope = info.scope;
    }

    // Missing arguments are undefined; locals start as their types do.
    for (uint32_t i = argc; i < arity; ++i) {
        args[i] = Val();
    }
    for (uint32_t i = arity; i < numLocals; ++i) {
        args[i] = defaultForCoerce(code->localInit[i]);
    }
    for (uint32_t i = numLocals; i < argc; ++i) {
        args[i] = Val();
    }
    const uint32_t given = std::min(argc, arity);
    for (uint32_t i = 0; i < given; ++i) {
        const Coerce coerce = code->paramCoerce[i];
        if (coerce == Coerce::None) {
            continue;
        }
        Val& arg = args[i];
        if (arg.tag == Val::Tag::Number) {
            // Numbers that are already so.
            if (coerce == Coerce::Number) continue;
            if (coerce == Coerce::Int && arg.num >= -2147483648.0 && arg.num <= 2147483647.0 &&
                arg.num == static_cast<double>(static_cast<int32_t>(arg.num))) {
                continue;
            }
        } else if (coerce == Coerce::Nullable && arg.tag != Val::Tag::Undefined) {
            continue;
        }
        arg = coerceValue(arg, coerce);
    }
    if (code->numCells) {
        frame.cells.resize(code->numCells);
        for (uint32_t i = 0; i < code->numCells; ++i) {
            frame.cells[i] = std::make_shared<Cell>();
            const int32_t init = code->cellInit[i];
            if (init >= 0) {
                frame.cells[i]->value = defaultForCoerce(static_cast<Coerce>(init));
            }
        }
        for (uint32_t i = 0; i < arity; ++i) {
            if (code->paramCells[i] >= 0) {
                frame.cells[static_cast<size_t>(code->paramCells[i])]->value = args[i];
            }
        }
    }
    if (code->restLocal >= 0) args[code->restLocal] = rest;
    if (code->restCell >= 0) frame.cells[static_cast<size_t>(code->restCell)]->value = rest;
    if (code->argumentsLocal >= 0) args[code->argumentsLocal] = arguments;
    if (code->argumentsCell >= 0) frame.cells[static_cast<size_t>(code->argumentsCell)]->value = arguments;
    if (code->selfLocal >= 0) args[code->selfLocal] = function;
    if (code->selfCell >= 0) frame.cells[static_cast<size_t>(code->selfCell)]->value = function;
    sp = args + numLocals;
}

bool VM::handleException(ScriptException& exception, size_t stopDepth) {
    while (frames.size() > stopDepth) {
        CallFrame& frame = frames.back();
        const size_t at = static_cast<size_t>(frame.ip - frame.codeStart);
        const size_t offset = at > 0 ? at - 1 : 0;
        for (const Handler& handler : frame.code->handlers) {
            if (offset >= handler.start && offset < handler.end) {
                Val* top = frame.base + frame.code->numLocals;
                for (Val* p = top; p < sp; ++p) *p = Val();
                sp = top;
                *sp++ = exception.value;
                frame.ip = frame.codeStart + handler.target;
                return true;
            }
        }
        for (Val* p = frame.result; p < sp; ++p) *p = Val();
        sp = frame.result;
        frames.pop_back();
    }
    return false;
}

void VM::run(size_t stopDepth) {
    std::optional<ScriptException> pending;
    std::optional<std::string> hostFailure;
    for (;;) {
        try {
            CallFrame* frame = &frames.back();
            const uint8_t* ip = frame->ip;
            Val* base = frame->base;
            BytecodeFunction* code = frame->code;
            const Val* constants = code->chunk.constants.data();

#define READ_U8() (*ip++)
#define READ_U16() (ip += 2, static_cast<uint16_t>(ip[-2] | (ip[-1] << 8)))
#define READ_S32()                                                                                        \
    (ip += 4, static_cast<int32_t>(static_cast<uint32_t>(ip[-4]) | (static_cast<uint32_t>(ip[-3]) << 8) |  \
                                   (static_cast<uint32_t>(ip[-2]) << 16) | (static_cast<uint32_t>(ip[-1]) << 24)))
#define RELOAD()                                    \
    do {                                            \
        frame = &frames.back();                     \
        ip = frame->ip;                             \
        base = frame->base;                         \
        code = frame->code;                         \
        constants = code->chunk.constants.data();   \
    } while (0)
#define NAME(i) (code->chunk.names[i])

            for (;;) {
                frame->ip = ip + 1;
                const OpCode op = static_cast<OpCode>(*ip++);
                switch (op) {
                    case OpCode::OP_CONSTANT: {
                        const Val& constant = constants[READ_U16()];
                        sp->tag = constant.tag;
                        sp->num = constant.num;
                        if (constant.ref) sp->ref = constant.ref;
                        ++sp;
                        break;
                    }
                    case OpCode::OP_UNDEFINED:
                        *sp++ = Val();
                        break;
                    case OpCode::OP_NULL:
                        *sp++ = Val::null();
                        break;
                    case OpCode::OP_TRUE:
                        *sp++ = Val::boolean(true);
                        break;
                    case OpCode::OP_FALSE:
                        *sp++ = Val::boolean(false);
                        break;
                    case OpCode::OP_ZERO:
                        sp->tag = Val::Tag::Number;
                        sp->num = 0;
                        ++sp;
                        break;
                    case OpCode::OP_ONE:
                        sp->tag = Val::Tag::Number;
                        sp->num = 1;
                        ++sp;
                        break;

                    case OpCode::OP_POP:
                        --sp;
                        if (sp->ref) sp->ref.reset();
                        break;
                    case OpCode::OP_DUP:
                        *sp = sp[-1];
                        ++sp;
                        break;
                    case OpCode::OP_DUP2:
                        sp[0] = sp[-2];
                        sp[1] = sp[-1];
                        sp += 2;
                        break;
                    case OpCode::OP_SWAP:
                        std::swap(sp[-1], sp[-2]);
                        break;
                    case OpCode::OP_DUP_X1: {
                        // a b -> b a b
                        Val b = sp[-1];
                        sp[0] = b;
                        sp[-1] = sp[-2];
                        sp[-2] = std::move(b);
                        ++sp;
                        break;
                    }
                    case OpCode::OP_DUP_X2: {
                        // a b c -> c a b c
                        Val c = sp[-1];
                        sp[0] = c;
                        sp[-1] = sp[-2];
                        sp[-2] = sp[-3];
                        sp[-3] = std::move(c);
                        ++sp;
                        break;
                    }

                    case OpCode::OP_GET_LOCAL: {
                        const Val& local = base[READ_U16()];
                        // The place is free (no reference held): numbers as they are.
                        sp->tag = local.tag;
                        sp->num = local.num;
                        if (local.ref) sp->ref = local.ref;
                        ++sp;
                        break;
                    }
                    case OpCode::OP_SET_LOCAL:
                        base[READ_U16()] = sp[-1];
                        break;
                    case OpCode::OP_PUT_LOCAL: {
                        Val& local = base[READ_U16()];
                        Val& top = *--sp;
                        if (top.ref || local.ref) {
                            local = std::move(top);
                            if (top.ref) top.ref.reset();
                        } else {
                            local.tag = top.tag;
                            local.num = top.num;
                        }
                        break;
                    }
                    case OpCode::OP_GET_CELL:
                        *sp++ = frame->cells[READ_U16()]->value;
                        break;
                    case OpCode::OP_SET_CELL:
                        frame->cells[READ_U16()]->value = sp[-1];
                        break;
                    case OpCode::OP_PUT_CELL:
                        frame->cells[READ_U16()]->value = std::move(*--sp);
                        *sp = Val();
                        break;
                    case OpCode::OP_GET_UPVALUE:
                        *sp++ = frame->closure->function->captured[READ_U16()]->value;
                        break;
                    case OpCode::OP_SET_UPVALUE:
                        frame->closure->function->captured[READ_U16()]->value = sp[-1];
                        break;
                    case OpCode::OP_PUT_UPVALUE:
                        frame->closure->function->captured[READ_U16()]->value = std::move(*--sp);
                        *sp = Val();
                        break;
                    case OpCode::OP_INC_LOCAL: {
                        Val& local = base[READ_U16()];
                        const double delta = static_cast<double>(READ_U8()) - 128.0;
                        const Coerce coerce = static_cast<Coerce>(READ_U8());
                        const double value = (local.tag == Val::Tag::Number ? local.num : toNumber(local)) + delta;
                        if (coerce == Coerce::Int) {
                            local = Val::number(toInt32(value));
                        } else if (coerce == Coerce::Uint) {
                            local = Val::number(toUint32(value));
                        } else if (coerce == Coerce::None || coerce == Coerce::Number) {
                            local = Val::number(value);
                        } else {
                            local = coerceValue(Val::number(value), coerce);
                        }
                        break;
                    }

                    case OpCode::OP_GET_NAME: {
                        const uint16_t name = READ_U16();
                        PropertyCache& cache = code->caches[READ_U16()];
                        // A global found before, while the names are as they were.
                        if (cache.version == namesVersion) {
                            const Val& self = frame->scopeSelf();
                            const ClassInfo* selfClass =
                                self.tag == Val::Tag::Object ? self.ref->cls.get() : nullptr;
                            if (selfClass == cache.cls && (self.tag != Val::Tag::Object ||
                                                           (!self.ref->host && self.ref->properties.empty())) &&
                                (frame->scope.isNullish() || frame->scope.ref == self.ref)) {
                                *sp++ = cache.value;
                                break;
                            }
                        }
                        Val value;
                        if (!lookupName(*frame, NAME(name), value, nullptr, &cache)) {
                            undefinedNameError(NAME(name));
                        }
                        *sp++ = std::move(value);
                        break;
                    }
                    case OpCode::OP_SET_NAME: {
                        const uint16_t name = READ_U16();
                        READ_U16();
                        assignName(*frame, NAME(name), sp[-1]);
                        break;
                    }
                    case OpCode::OP_TYPEOF_NAME: {
                        const uint16_t name = READ_U16();
                        Val value;
                        if (lookupName(*frame, NAME(name), value, nullptr)) {
                            *sp++ = Val::string(typeOf(value));
                        } else {
                            *sp++ = Val::string("undefined");
                        }
                        break;
                    }
                    case OpCode::OP_DEFINE_VAR: {
                        const uint16_t name = READ_U16();
                        defineVar(*frame, NAME(name), sp[-1], true);
                        *--sp = Val();
                        break;
                    }
                    case OpCode::OP_DEFINE_FUNCTION: {
                        const uint16_t name = READ_U16();
                        defineVar(*frame, NAME(name), sp[-1], false);
                        *--sp = Val();
                        break;
                    }
                    case OpCode::OP_GET_QNAME: {
                        const uint16_t name = READ_U16();
                        Val value = lookupQualified(NAME(name), true);
                        if (value.isUndefined()) {
                            undefinedNameError(NAME(name));
                        }
                        *sp++ = std::move(value);
                        break;
                    }

                    case OpCode::OP_GET_PROP: {
                        const uint16_t name = READ_U16();
                        PropertyCache& cache = code->caches[READ_U16()];
                        Val& object = sp[-1];
                        frame->ip = ip;
                        const int done = cachedGet(object, cache, &object);
                        if (done == 1) {
                            break;
                        }
                        if (done == 2) {
                            RELOAD();
                            break;
                        }
                        Val result = getProp(object, NAME(name), &cache);
                        sp[-1] = std::move(result);
                        break;
                    }
                    case OpCode::OP_SET_PROP: {
                        const uint16_t name = READ_U16();
                        PropertyCache& cache = code->caches[READ_U16()];
                        frame->ip = ip;
                        // obj value -> value: the object is moved out, its place
                        // taking the value.
                        Val object = std::move(sp[-2]);
                        sp[-2] = std::move(sp[-1]);
                        --sp;
                        const int done = cachedSet(object, cache, sp - 1);
                        if (done == 2) {
                            RELOAD();
                            break;
                        }
                        if (done == 0) {
                            setProp(object, NAME(name), sp[-1], &cache);
                        }
                        break;
                    }
                    case OpCode::OP_DELETE_PROP: {
                        const uint16_t name = READ_U16();
                        const bool done = deleteProp(sp[-1], NAME(name));
                        sp[-1] = Val::boolean(done);
                        break;
                    }
                    case OpCode::OP_GET_INDEX: {
                        Val& object = sp[-2];
                        const Val& key = sp[-1];
                        if (object.tag == Val::Tag::Object && key.tag == Val::Tag::Number &&
                            object.ref->type == ValueType::Array) {
                            const double index = key.num;
                            std::vector<Val>& elements = object.ref->elements;
                            if (index >= 0 && index < static_cast<double>(elements.size()) &&
                                index == static_cast<double>(static_cast<size_t>(index))) {
                                Val element = elements[static_cast<size_t>(index)];
                                *--sp = Val();
                                sp[-1] = std::move(element);
                                break;
                            }
                        }
                        frame->ip = ip;
                        Val result = getIndexOf(object, key);
                        *--sp = Val();
                        sp[-1] = std::move(result);
                        break;
                    }
                    case OpCode::OP_SET_INDEX: {
                        Val& object = sp[-3];
                        const Val& key = sp[-2];
                        if (object.tag == Val::Tag::Object && key.tag == Val::Tag::Number &&
                            object.ref->type == ValueType::Array &&
                            (!object.ref->cls || !object.ref->cls->isVector)) {
                            const double index = key.num;
                            std::vector<Val>& elements = object.ref->elements;
                            if (index >= 0 && index < static_cast<double>(elements.size()) &&
                                index == static_cast<double>(static_cast<size_t>(index))) {
                                elements[static_cast<size_t>(index)] = sp[-1];
                                sp[-3] = std::move(sp[-1]);
                                sp[-2] = Val();
                                sp[-1] = Val();
                                sp -= 2;
                                break;
                            }
                        }
                        frame->ip = ip;
                        setIndexOf(object, key, sp[-1]);
                        sp[-3] = std::move(sp[-1]);
                        sp[-2] = Val();
                        sp[-1] = Val();
                        sp -= 2;
                        break;
                    }
                    case OpCode::OP_DELETE_INDEX: {
                        const bool done = deleteIndex(sp[-2], sp[-1]);
                        *--sp = Val();
                        sp[-1] = Val::boolean(done);
                        break;
                    }
                    case OpCode::OP_GET_SUPER: {
                        const uint16_t name = READ_U16();
                        ClassInfo* base_ = frame->owner ? frame->owner->base.get() : nullptr;
                        const Trait* trait = base_ ? base_->findTrait(NAME(name)) : nullptr;
                        const Val self = frame->scopeSelf().isUndefined() ? frame->thisVal : frame->scopeSelf();
                        if (trait) {
                            *sp++ = readTrait(self, *trait, false);
                        } else {
                            *sp++ = getProp(self, NAME(name), nullptr);
                        }
                        break;
                    }
                    case OpCode::OP_SET_SUPER: {
                        const uint16_t name = READ_U16();
                        ClassInfo* base_ = frame->owner ? frame->owner->base.get() : nullptr;
                        const Trait* trait = base_ ? base_->findTrait(NAME(name)) : nullptr;
                        const Val self = frame->scopeSelf().isUndefined() ? frame->thisVal : frame->scopeSelf();
                        if (trait) {
                            writeTrait(self, *trait, sp[-1], NAME(name), false);
                        } else {
                            setProp(self, NAME(name), sp[-1], nullptr);
                        }
                        break;
                    }

                    case OpCode::OP_GET_LOCAL_PROP: {
                        const uint16_t slot = READ_U16();
                        const uint16_t name = READ_U16();
                        PropertyCache& cache = code->caches[READ_U16()];
                        const Val& object = base[slot];
                        frame->ip = ip;
                        const int done = cachedGet(object, cache, sp);
                        if (done == 1) {
                            ++sp;
                            break;
                        }
                        if (done == 2) {
                            RELOAD();
                            break;
                        }
                        Val result = getProp(object, NAME(name), &cache);
                        *sp++ = std::move(result);
                        break;
                    }
                    case OpCode::OP_GET_THIS_PROP: {
                        const uint16_t name = READ_U16();
                        PropertyCache& cache = code->caches[READ_U16()];
                        const Val& object = frame->scopeSelf().isUndefined() ? frame->thisVal : frame->scopeSelf();
                        frame->ip = ip;
                        const int done = cachedGet(object, cache, sp);
                        if (done == 1) {
                            ++sp;
                            break;
                        }
                        if (done == 2) {
                            RELOAD();
                            break;
                        }
                        Val result = getProp(object, NAME(name), &cache);
                        *sp++ = std::move(result);
                        break;
                    }
                    case OpCode::OP_SET_LOCAL_PROP:
                    case OpCode::OP_SET_THIS_PROP: {
                        const uint16_t slot = op == OpCode::OP_SET_LOCAL_PROP ? READ_U16() : 0;
                        const uint16_t name = READ_U16();
                        PropertyCache& cache = code->caches[READ_U16()];
                        const Val& object =
                            op == OpCode::OP_SET_LOCAL_PROP
                                ? base[slot]
                                : (frame->scopeSelf().isUndefined() ? frame->thisVal : frame->scopeSelf());
                        frame->ip = ip;
                        const int done = cachedSet(object, cache, sp - 1);
                        if (done == 2) {
                            RELOAD();
                            break;
                        }
                        if (done == 0) {
                            setProp(object, NAME(name), sp[-1], &cache);
                        }
                        break;
                    }
                    case OpCode::OP_CALL_THIS_PROP:
                    case OpCode::OP_CALL_LOCAL_PROP: {
                        const uint16_t slot = op == OpCode::OP_CALL_LOCAL_PROP ? READ_U16() : 0;
                        const uint16_t name = READ_U16();
                        const uint8_t argc = READ_U8();
                        PropertyCache& cache = code->caches[READ_U16()];
                        Val* result = sp - argc - 1;
                        const Val& object =
                            op == OpCode::OP_CALL_LOCAL_PROP
                                ? base[slot]
                                : (frame->scopeSelf().isUndefined() ? frame->thisVal : frame->scopeSelf());
                        frame->ip = ip;
                        const int done = cachedCall(object, cache, result + 1, argc, result);
                        if (done == 2) {
                            RELOAD();
                            break;
                        }
                        if (done == 0 && callProp(object, NAME(name), result + 1, argc, result, &cache)) {
                            RELOAD();
                        }
                        break;
                    }
                    case OpCode::OP_ADD: {
                        Val& a = sp[-2];
                        Val& b = sp[-1];
                        if (a.tag == Val::Tag::Number && b.tag == Val::Tag::Number) {
                            a.num += b.num;
                            --sp;
                            break;
                        }
                        a = add(a, b);
                        *--sp = Val();
                        break;
                    }
#define NUMERIC_OP(OPERATION)                                                     \
    {                                                                             \
        Val& a = sp[-2];                                                          \
        Val& b = sp[-1];                                                          \
        if (a.tag == Val::Tag::Number && b.tag == Val::Tag::Number) {             \
            const double x = a.num;                                               \
            const double y = b.num;                                               \
            a.num = OPERATION;                                                    \
            --sp;                                                                 \
            break;                                                                \
        }                                                                         \
        const double x = a.tag == Val::Tag::Number ? a.num : toNumber(a);         \
        const double y = b.tag == Val::Tag::Number ? b.num : toNumber(b);         \
        a = Val::number(OPERATION);                                               \
        *--sp = Val();                                                            \
        break;                                                                    \
    }
                    case OpCode::OP_SUBTRACT:
                        NUMERIC_OP(x - y)
                    case OpCode::OP_MULTIPLY:
                        NUMERIC_OP(x * y)
                    case OpCode::OP_DIVIDE:
                        NUMERIC_OP(x / y)
                    case OpCode::OP_MODULO:
                        NUMERIC_OP(std::fmod(x, y))
                    case OpCode::OP_BIT_AND:
                        NUMERIC_OP(static_cast<double>(toInt32(x) & toInt32(y)))
                    case OpCode::OP_BIT_OR:
                        NUMERIC_OP(static_cast<double>(toInt32(x) | toInt32(y)))
                    case OpCode::OP_BIT_XOR:
                        NUMERIC_OP(static_cast<double>(toInt32(x) ^ toInt32(y)))
                    case OpCode::OP_LEFT_SHIFT:
                        NUMERIC_OP(static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(toInt32(x))
                                                                            << (toUint32(y) & 31))))
                    case OpCode::OP_RIGHT_SHIFT:
                        NUMERIC_OP(static_cast<double>(toInt32(x) >> (toUint32(y) & 31)))
                    case OpCode::OP_UNSIGNED_RIGHT_SHIFT:
                        NUMERIC_OP(static_cast<double>(toUint32(x) >> (toUint32(y) & 31)))
#undef NUMERIC_OP
                    case OpCode::OP_NEGATE: {
                        Val& a = sp[-1];
                        a = Val::number(-(a.tag == Val::Tag::Number ? a.num : toNumber(a)));
                        break;
                    }
                    case OpCode::OP_TO_NUMBER: {
                        Val& a = sp[-1];
                        if (a.tag != Val::Tag::Number) a = Val::number(toNumber(a));
                        break;
                    }
                    case OpCode::OP_INCREMENT: {
                        Val& a = sp[-1];
                        a = Val::number((a.tag == Val::Tag::Number ? a.num : toNumber(a)) + 1);
                        break;
                    }
                    case OpCode::OP_DECREMENT: {
                        Val& a = sp[-1];
                        a = Val::number((a.tag == Val::Tag::Number ? a.num : toNumber(a)) - 1);
                        break;
                    }
                    case OpCode::OP_BIT_NOT: {
                        Val& a = sp[-1];
                        a = Val::number(static_cast<double>(~a.toInt32()));
                        break;
                    }
                    case OpCode::OP_NOT: {
                        Val& a = sp[-1];
                        a = Val::boolean(!a.toBoolean());
                        break;
                    }

                    case OpCode::OP_EQUAL:
                    case OpCode::OP_NOT_EQUAL: {
                        Val& a = sp[-2];
                        const Val& b = sp[-1];
                        bool equal;
                        if (a.tag == Val::Tag::Number && b.tag == Val::Tag::Number) {
                            equal = a.num == b.num;
                        } else {
                            equal = looselyEqual(a, b);
                        }
                        a = Val::boolean(op == OpCode::OP_EQUAL ? equal : !equal);
                        *--sp = Val();
                        break;
                    }
                    case OpCode::OP_STRICT_EQUAL:
                    case OpCode::OP_STRICT_NOT_EQUAL: {
                        Val& a = sp[-2];
                        bool equal = a.strictEquals(sp[-1]);
                        a = Val::boolean(op == OpCode::OP_STRICT_EQUAL ? equal : !equal);
                        *--sp = Val();
                        break;
                    }
                    case OpCode::OP_LESS:
                    case OpCode::OP_GREATER:
                    case OpCode::OP_LESS_EQUAL:
                    case OpCode::OP_GREATER_EQUAL: {
                        Val& a = sp[-2];
                        const Val& b = sp[-1];
                        bool result;
                        if (a.tag == Val::Tag::Number && b.tag == Val::Tag::Number) {
                            switch (op) {
                                case OpCode::OP_LESS: result = a.num < b.num; break;
                                case OpCode::OP_GREATER: result = a.num > b.num; break;
                                case OpCode::OP_LESS_EQUAL: result = a.num <= b.num; break;
                                default: result = a.num >= b.num; break;
                            }
                            a.tag = Val::Tag::Boolean;
                            a.flag = result;
                            --sp;
                            break;
                        } else {
                            bool undefinedResult = false;
                            switch (op) {
                                case OpCode::OP_LESS:
                                    result = lessThan(a, b, undefinedResult);
                                    break;
                                case OpCode::OP_GREATER:
                                    result = lessThan(b, a, undefinedResult);
                                    break;
                                case OpCode::OP_LESS_EQUAL:
                                    result = !lessThan(b, a, undefinedResult);
                                    break;
                                default:
                                    result = !lessThan(a, b, undefinedResult);
                                    break;
                            }
                            if (undefinedResult) result = false;
                        }
                        a = Val::boolean(result);
                        *--sp = Val();
                        break;
                    }

                    case OpCode::OP_TYPEOF: {
                        Val& a = sp[-1];
                        a = Val::string(typeOf(a));
                        break;
                    }
                    case OpCode::OP_INSTANCEOF: {
                        Val& a = sp[-2];
                        const Val& type = sp[-1];
                        bool result = false;
                        if (type.isClass() && a.isObject()) {
                            const ClassInfo* target = type->cls.get();
                            for (const ClassInfo* at = classOf(a); at; at = at->base.get()) {
                                if (at == target) {
                                    result = true;
                                    break;
                                }
                            }
                        } else if (!type.isObject()) {
                            throwError("TypeError", "Error #1040: The right-hand side of instanceof must be a class or function.",
                                       1040);
                        }
                        a = Val::boolean(result);
                        *--sp = Val();
                        break;
                    }
                    case OpCode::OP_IN: {
                        Val& key = sp[-2];
                        const Val& object = sp[-1];
                        if (object.isNullish()) {
                            nullError(object, key.toString());
                        }
                        key = Val::boolean(hasKey(object, key));
                        *--sp = Val();
                        break;
                    }
                    case OpCode::OP_IS: {
                        Val& a = sp[-2];
                        a = Val::boolean(isType(a, sp[-1]));
                        *--sp = Val();
                        break;
                    }
                    case OpCode::OP_AS: {
                        Val& a = sp[-2];
                        if (!isType(a, sp[-1])) a = Val::null();
                        *--sp = Val();
                        break;
                    }
                    case OpCode::OP_COERCE: {
                        const Coerce coerce = static_cast<Coerce>(READ_U8());
                        Val& a = sp[-1];
                        if (coerce == Coerce::Number && a.tag == Val::Tag::Number) {
                            break;
                        }
                        if (coerce == Coerce::Int && a.tag == Val::Tag::Number) {
                            const double n = a.num;
                            if (!(n >= -2147483648.0 && n <= 2147483647.0 &&
                                  n == static_cast<double>(static_cast<int32_t>(n)))) {
                                a.num = toInt32(n);
                            }
                        } else if (coerce == Coerce::String && a.isObject()) {
                            a = Val::string(toString(a));
                        } else if ((coerce == Coerce::Number || coerce == Coerce::Int || coerce == Coerce::Uint) &&
                                   a.isObject()) {
                            a = coerceValue(Val::number(toNumber(a)), coerce);
                        } else {
                            a = coerceValue(a, coerce);
                        }
                        break;
                    }

                    case OpCode::OP_JUMP: {
                        const int32_t offset = READ_S32();
                        ip += offset;
                        break;
                    }
                    case OpCode::OP_JUMP_IF_FALSE: {
                        const int32_t offset = READ_S32();
                        Val& a = *--sp;
                        const bool truth = a.tag == Val::Tag::Boolean ? a.flag : a.toBoolean();
                        if (a.ref) a.ref.reset();
                        if (!truth) ip += offset;
                        break;
                    }
                    case OpCode::OP_JUMP_IF_TRUE: {
                        const int32_t offset = READ_S32();
                        Val& a = *--sp;
                        const bool truth = a.tag == Val::Tag::Boolean ? a.flag : a.toBoolean();
                        if (a.ref) a.ref.reset();
                        if (truth) ip += offset;
                        break;
                    }
                    case OpCode::OP_JUMP_IF_FALSE_KEEP: {
                        const int32_t offset = READ_S32();
                        if (!sp[-1].toBoolean()) {
                            ip += offset;
                        } else {
                            *--sp = Val();
                        }
                        break;
                    }
                    case OpCode::OP_JUMP_IF_TRUE_KEEP: {
                        const int32_t offset = READ_S32();
                        if (sp[-1].toBoolean()) {
                            ip += offset;
                        } else {
                            *--sp = Val();
                        }
                        break;
                    }
                    case OpCode::OP_JUMP_IF_NOT_NULLISH_KEEP: {
                        const int32_t offset = READ_S32();
                        if (!sp[-1].isNullish()) {
                            ip += offset;
                        } else {
                            *--sp = Val();
                        }
                        break;
                    }

                    case OpCode::OP_CALL: {
                        const uint8_t argc = READ_U8();
                        Val* callee = sp - argc - 1;
                        frame->ip = ip;
                        if (invoke(*callee, Val(), callee + 1, argc, callee, kModeNormal, Val(), true)) {
                            RELOAD();
                        }
                        break;
                    }
                    case OpCode::OP_CALL_PROP: {
                        const uint16_t name = READ_U16();
                        const uint8_t argc = READ_U8();
                        PropertyCache& cache = code->caches[READ_U16()];
                        Val* object = sp - argc - 1;
                        frame->ip = ip;
                        const int done = cachedCall(*object, cache, object + 1, argc, object);
                        if (done == 2) {
                            RELOAD();
                            break;
                        }
                        if (done == 0 && callProp(*object, NAME(name), object + 1, argc, object, &cache)) {
                            RELOAD();
                        }
                        break;
                    }
                    case OpCode::OP_CALL_NAME: {
                        const uint16_t name = READ_U16();
                        const uint8_t argc = READ_U8();
                        PropertyCache& cache = code->caches[READ_U16()];
                        frame->ip = ip;
                        Val function;
                        Val receiver;
                        bool cached = false;
                        if (cache.version == namesVersion) {
                            const Val& self = frame->scopeSelf();
                            const ClassInfo* selfClass =
                                self.tag == Val::Tag::Object ? self.ref->cls.get() : nullptr;
                            if (selfClass == cache.cls && (self.tag != Val::Tag::Object ||
                                                           (!self.ref->host && self.ref->properties.empty())) &&
                                (frame->scope.isNullish() || frame->scope.ref == self.ref)) {
                                function = cache.value;
                                cached = true;
                            }
                        }
                        if (!cached && !lookupName(*frame, NAME(name), function, &receiver, &cache)) {
                            undefinedNameError(NAME(name));
                        }
                        if (!function.isFunction() && !function.isClass()) {
                            notAFunctionError(NAME(name));
                        }
                        // The arguments move up one, for the result's place.
                        Val* args = sp - argc;
                        for (Val* p = sp; p > args; --p) {
                            *p = std::move(p[-1]);
                        }
                        ++sp;
                        Val* result = args;
                        *result = Val();
                        if (invoke(function, receiver, args + 1, argc, result)) {
                            RELOAD();
                        }
                        break;
                    }
                    case OpCode::OP_CALL_INDEX: {
                        const uint8_t argc = READ_U8();
                        Val* object = sp - argc - 2;
                        frame->ip = ip;
                        Val function = getIndexOf(object[0], object[1]);
                        if (!function.isFunction() && !function.isClass()) {
                            notAFunctionError(object[1].toString());
                        }
                        Val self = object[0];
                        // object key args -> the key's place is dropped.
                        for (Val* p = object + 1; p < sp - 1; ++p) {
                            *p = std::move(p[1]);
                        }
                        *--sp = Val();
                        if (invoke(function, self, object + 1, argc, object)) {
                            RELOAD();
                        }
                        break;
                    }
                    case OpCode::OP_CALL_SUPER: {
                        const uint16_t name = READ_U16();
                        const uint8_t argc = READ_U8();
                        frame->ip = ip;
                        ClassInfo* base_ = frame->owner ? frame->owner->base.get() : nullptr;
                        const Trait* trait = base_ ? base_->findTrait(NAME(name)) : nullptr;
                        const Val self = frame->scopeSelf().isUndefined() ? frame->thisVal : frame->scopeSelf();
                        Val function;
                        if (trait) {
                            function = trait->kind == TraitKind::Method ? trait->method : readTrait(self, *trait, false);
                        } else {
                            function = getProp(self, NAME(name), nullptr);
                        }
                        if (!function.isFunction()) {
                            notAFunctionError(NAME(name));
                        }
                        Val* args = sp - argc;
                        for (Val* p = sp; p > args; --p) {
                            *p = std::move(p[-1]);
                        }
                        ++sp;
                        *args = Val();
                        if (invoke(function, self, args + 1, argc, args)) {
                            RELOAD();
                        }
                        break;
                    }
                    case OpCode::OP_SUPER_CTOR: {
                        const uint8_t argc = READ_U8();
                        frame->ip = ip;
                        Val* args = sp - argc;
                        for (Val* p = sp; p > args; --p) {
                            *p = std::move(p[-1]);
                        }
                        ++sp;
                        *args = Val();
                        const size_t depth = frames.size();
                        superConstruct(*frame, args + 1, argc, args);
                        if (frames.size() > depth) {
                            RELOAD();
                        }
                        break;
                    }
                    case OpCode::OP_NEW: {
                        const uint8_t argc = READ_U8();
                        Val* callee = sp - argc - 1;
                        frame->ip = ip;
                        const size_t depth = frames.size();
                        Val cls = *callee;
                        constructInto(cls, callee + 1, argc, callee);
                        if (frames.size() > depth) {
                            RELOAD();
                        }
                        break;
                    }
                    case OpCode::OP_RETURN:
                    case OpCode::OP_RETURN_UNDEFINED: {
                        Val result;
                        if (op == OpCode::OP_RETURN) {
                            result = std::move(*--sp);
                            *sp = Val();
                        }
                        CallFrame& done = *frame;
                        switch (done.mode) {
                            case kModeSaved:
                                result = done.saved;
                                break;
                            case kModeDiscard:
                                result = Val();
                                break;
                            case kModeConstructFunction:
                                if (!result.isObject()) result = done.saved;
                                break;
                            default:
                                break;
                        }
                        Val* slot = done.result;
                        for (Val* p = slot; p < sp; ++p) *p = Val();
                        *slot = std::move(result);
                        sp = slot + 1;
                        frames.pop_back();
                        if (frames.size() <= stopDepth) {
                            return;
                        }
                        RELOAD();
                        break;
                    }
                    case OpCode::OP_THIS: {
                        const Val& self = frame->thisVal;
                        if (self.isUndefined()) {
                            *sp++ = frame->scope;
                        } else {
                            *sp++ = self;
                        }
                        break;
                    }
                    case OpCode::OP_SCOPE_THIS:
                        *sp++ = frame->scopeSelf().isUndefined() ? frame->thisVal : frame->scopeSelf();
                        break;
                    case OpCode::OP_OWNER_CLASS: {
                        Val owner;
                        if (frame->owner) {
                            if (ValuePtr object = frame->owner->classObject.lock()) owner = Val(object);
                        }
                        *sp++ = std::move(owner);
                        break;
                    }
                    case OpCode::OP_CLOSURE: {
                        const uint16_t index = READ_U16();
                        const auto& nested = code->functions[index];
                        Val closure = makeClosure(nested, nullptr);
                        FunctionInfo& info = *closure->function;
                        if (frame->closure && frame->closure->function) {
                            info.owner = frame->closure->function->owner;
                        }
                        info.scopeThis = frame->scopeSelf();
                        info.scope = frame->scope;
                        info.captured.reserve(nested->upvalues.size());
                        for (const UpvalueSource& source : nested->upvalues) {
                            if (source.fromCell) {
                                info.captured.push_back(frame->cells[source.index]);
                            } else {
                                info.captured.push_back(frame->closure->function->captured[source.index]);
                            }
                        }
                        *sp++ = std::move(closure);
                        break;
                    }
                    case OpCode::OP_DEFAULT_ARG: {
                        const uint16_t param = READ_U16();
                        const int32_t offset = READ_S32();
                        if (frame->argc > param) ip += offset;
                        break;
                    }

                    case OpCode::OP_NEW_ARRAY: {
                        const uint16_t count = READ_U16();
                        Val* first = sp - count;
                        std::vector<Val> elements;
                        elements.reserve(count);
                        for (Val* p = first; p < sp; ++p) {
                            elements.push_back(std::move(*p));
                            *p = Val();
                        }
                        sp = first;
                        *sp++ = newArray(std::move(elements));
                        break;
                    }
                    case OpCode::OP_NEW_OBJECT: {
                        const uint16_t pairs = READ_U16();
                        Val* first = sp - 2 * pairs;
                        Val object = newObject();
                        for (Val* p = first; p < sp; p += 2) {
                            object->properties[p[0].toString()] = p[1];
                        }
                        for (Val* p = first; p < sp; ++p) *p = Val();
                        sp = first;
                        *sp++ = std::move(object);
                        break;
                    }
                    case OpCode::OP_NEW_VECTOR: {
                        const uint16_t count = READ_U16();
                        Val* type = sp - count - 1;
                        frame->ip = ip;
                        auto cls = vectorClass(*type);
                        Val vector = allocate(cls);
                        vector->elements.reserve(count);
                        for (Val* p = type + 1; p < sp; ++p) {
                            vector->elements.push_back(coerceValue(*p, cls->elementCoerce));
                        }
                        for (Val* p = type; p < sp; ++p) *p = Val();
                        sp = type;
                        *sp++ = std::move(vector);
                        break;
                    }
                    case OpCode::OP_NEW_REGEXP: {
                        const uint16_t pattern = READ_U16();
                        const uint16_t flags = READ_U16();
                        frame->ip = ip;
                        Val regexp = makeRegExpValue(constants[pattern], constants[flags]);
                        *sp++ = std::move(regexp);
                        break;
                    }
                    case OpCode::OP_APPLY_TYPE: {
                        const uint8_t count = READ_U8();
                        Val* first = sp - count - 1;
                        frame->ip = ip;
                        std::vector<Val> types(first + 1, sp);
                        Val applied = applyType(*first, types);
                        for (Val* p = first; p < sp; ++p) *p = Val();
                        sp = first;
                        *sp++ = std::move(applied);
                        break;
                    }
                    case OpCode::OP_DEFINE_CLASS: {
                        const uint16_t index = READ_U16();
                        frame->ip = ip;
                        auto klass = code->classes[index];
                        Val defined = defineClassFromTemplate(klass);
                        *sp++ = std::move(defined);
                        break;
                    }

                    case OpCode::OP_THROW: {
                        ScriptException exception{std::move(sp[-1])};
                        *--sp = Val();
                        throw exception;
                    }
                    case OpCode::OP_ITER_INIT: {
                        const bool each = READ_U8() != 0;
                        sp[-1] = makeIterator(sp[-1], each);
                        break;
                    }
                    case OpCode::OP_ITER_NEXT: {
                        const uint16_t local = READ_U16();
                        const int32_t offset = READ_S32();
                        Val& iterator = base[local];
                        Value* state = iterator.ref.get();
                        // elements: the keys; slots[0]: the object; numberValue: where it is.
                        const size_t at = static_cast<size_t>(state->numberValue);
                        if (at >= state->elements.size()) {
                            ip += offset;
                            break;
                        }
                        state->numberValue += 1;
                        if (state->boolValue) {
                            frame->ip = ip;
                            Val value = getIndexOf(state->slots[0], state->elements[at]);
                            *sp++ = std::move(value);
                        } else {
                            *sp++ = state->elements[at];
                        }
                        break;
                    }
                    case OpCode::OP_NOP:
                        break;
                    default:
                        throwError("Error", "Unknown opcode", 0);
                }
            }
#undef READ_U8
#undef READ_U16
#undef READ_S32
#undef RELOAD
#undef NAME
        } catch (ScriptException& exception) {
            pending = std::move(exception);
        } catch (const std::bad_alloc&) {
            hostFailure = "Error #1000: The system is out of memory.";
        } catch (const std::exception& failed) {
            hostFailure = failed.what();
        }
        // Out of the catch (see runCall): handled here, or on to the
        // caller.
        if (!pending) {
            pending = ScriptException{makeError("Error", *hostFailure, hostFailure->compare(0, 11, "Error #1000") == 0 ? 1000 : 0)};
            hostFailure.reset();
        }
        if (pending->line < 0) {
            pending->line = currentLine();
        }
        if (!handleException(*pending, stopDepth)) {
            ScriptException exception = std::move(*pending);
            throw exception;
        }
        pending.reset();
    }
}

// Inline caches

int VM::cachedGet(const Val& object, const PropertyCache& cache, Val* out) {
    if (object.tag != Val::Tag::Object) return 0;
    Value* value = object.ref.get();
    if (value->cls.get() != cache.cls || !cache.cls || cache.onClass || value->type == ValueType::Class) return 0;
    const Trait* trait = cache.trait;
    if (trait->kind == TraitKind::Slot) {
        const Val& field = value->slots[trait->slot];
        if (out == &object) {
            *out = field;
        } else {
            // A free place: no reference to let go of.
            out->tag = field.tag;
            out->num = field.num;
            if (field.ref) out->ref = field.ref;
        }
        return 1;
    }
    if (trait->kind == TraitKind::Accessor && trait->getter.isObject()) {
        const Val& getter = trait->getter;
        FunctionInfo& info = *getter.ref->function;
        if (info.code) {
            Val self = object;
            pushFrame(getter, info, self, out + 1, 0, out, kModeNormal, Val(), true);
            return 2;
        }
        if (info.native) {
            Val result = info.native(*this, object, Args());
            *out = std::move(result);
            return 1;
        }
    }
    return 0;
}

int VM::cachedSet(const Val& object, const PropertyCache& cache, Val* value) {
    if (object.tag != Val::Tag::Object) return 0;
    Value* self = object.ref.get();
    if (self->cls.get() != cache.cls || !cache.cls || cache.onClass || self->type == ValueType::Class) return 0;
    const Trait* trait = cache.trait;
    if (trait->kind == TraitKind::Slot) {
        Val& slot = self->slots[trait->slot];
        if (trait->coerce == Coerce::None) {
            slot = *value;
        } else if (trait->coerce == Coerce::Number && value->tag == Val::Tag::Number) {
            if (slot.ref) slot.ref.reset();
            slot.tag = Val::Tag::Number;
            slot.num = value->num;
        } else {
            slot = coerceValue(*value, trait->coerce);
        }
        return 1;
    }
    if (trait->kind == TraitKind::Accessor && trait->setter.isObject()) {
        const Val& setter = trait->setter;
        FunctionInfo& info = *setter.ref->function;
        if (info.code) {
            // The setter runs in a frame; the assignment's value is what
            // it gives.
            Val receiver = object;
            value[1] = *value;
            pushFrame(setter, info, receiver, value + 1, 1, value, kModeSaved, *value, true);
            return 2;
        }
        if (info.native) {
            info.native(*this, object, Args(value, 1));
            return 1;
        }
    }
    return 0;
}

int VM::cachedCall(const Val& object, const PropertyCache& cache, Val* args, uint32_t argc, Val* result) {
    if (object.tag != Val::Tag::Object) return 0;
    Value* value = object.ref.get();
    if (value->cls.get() != cache.cls || !cache.cls || cache.trait->kind != TraitKind::Method ||
        cache.onClass != (value->type == ValueType::Class)) {
        return 0;
    }
    const Val& method = cache.trait->method;
    FunctionInfo& info = *method.ref->function;
    if (info.code) {
        pushFrame(method, info, object, args, argc, result, kModeNormal, Val(), true);
        return 2;
    }
    if (info.native) {
        Val made = info.native(*this, object, Args(args, argc));
        for (Val* p = result; p < sp; ++p) *p = Val();
        *result = std::move(made);
        sp = result + 1;
        return 1;
    }
    return 0;
}

// Errors the loop raises, kept out of it.

void VM::undefinedNameError(const std::string& name) {
    throwError("ReferenceError", "Error #1065: Variable " + name + " is not defined.", 1065);
}

void VM::notAFunctionError(const std::string& name) {
    throwError("TypeError", "Error #1006: " + name + " is not a function.", 1006);
}

Val VM::makeRegExpValue(const Val& pattern, const Val& flags) {
    return construct(Val(core.regexp->classObject.lock()), std::vector<Val>{pattern, flags});
}

// Construction

void VM::constructInto(const Val& cls, Val* args, uint32_t argc, Val* result) {
    if (cls.isClass()) {
        ClassInfo* info = cls->cls.get();
        if (info->isInterface) {
            throwError("TypeError", "Error #1001: The interface " + info->shortName + " cannot be instantiated.",
                       1001);
        }
        if (info->isVector && info->elementTypeName.empty() && !info->elementClass) {
            throwError("TypeError", "Error #1007: Instantiation attempted on a non-constructor.", 1007);
        }
        Val instance = allocate(cls->cls);
        if (info->constructor.isObject()) {
            const Val& constructor = info->constructor;
            pushFrame(constructor, *constructor.ref->function, instance, args, argc, result, kModeSaved, instance,
                      true);
            return;
        }
        Val replaced;
        runNativeConstructor(info, instance, Args(args, argc), replaced);
        for (Val* p = result; p < sp; ++p) *p = Val();
        *result = replaced.isUndefined() ? std::move(instance) : std::move(replaced);
        sp = result + 1;
        return;
    }
    if (cls.isObject() && cls->function && cls->function->code) {
        // new on a plain function (ActionScript 1).
        Val instance = newObject();
        pushFrame(cls, *cls->function, instance, args, argc, result, kModeConstructFunction, instance);
        return;
    }
    if (cls.isObject() && (cls->nativeFunc || (cls->function && cls->function->native))) {
        // A host's constructor: what it makes.
        Val instance = newObject();
        Val made = callNative(cls, instance, args, argc);
        for (Val* p = result; p < sp; ++p) *p = Val();
        *result = made.isObject() ? std::move(made) : std::move(instance);
        sp = result + 1;
        return;
    }
    throwError("TypeError", "Error #1007: Instantiation attempted on a non-constructor.", 1007);
}

void VM::runNativeConstructor(ClassInfo* cls, const Val& instance, Args args, Val& replaced) {
    for (ClassInfo* at = cls; at; at = at->base.get()) {
        if (at->constructor.isObject()) {
            // A script class above a native one: its constructor, at once.
            callFunction(at->constructor, instance, args);
            return;
        }
        if (at->nativeConstructor) {
            replaced = at->nativeConstructor(*this, instance, args);
            return;
        }
    }
}

void VM::superConstruct(CallFrame& frame, Val* args, uint32_t argc, Val* result) {
    ClassInfo* base = frame.owner ? frame.owner->base.get() : nullptr;
    const Val self = frame.thisVal;
    if (base && self.isObject()) {
        if (base->constructor.isObject()) {
            const Val& constructor = base->constructor;
            pushFrame(constructor, *constructor.ref->function, self, args, argc, result, kModeDiscard, Val(), true);
            return;
        }
        Val replaced;
        runNativeConstructor(base, self, Args(args, argc), replaced);
    }
    for (Val* p = result; p < sp; ++p) *p = Val();
    *result = Val();
    sp = result + 1;
}

Val VM::construct(const Val& cls, Args args) {
    growStack(sp + args.size() + 2);
    char marker = 0;
    enterNative(&marker);
    struct Depth {
        int& depth;
        ~Depth() { --depth; }
    } depth{nativeDepth};
    Val* result = sp++;
    *result = Val();
    Val* argStart = sp;
    for (const Val& arg : args) {
        *sp++ = arg;
    }
    const size_t depthBefore = frames.size();
    struct Unwind {
        VM& vm;
        size_t depth;
        Val* result;
        bool done = false;
        ~Unwind() {
            if (!done) vm.unwindTo(depth, result);
        }
    } unwind{*this, depthBefore, result};
    constructInto(cls, argStart, static_cast<uint32_t>(args.size()), result);
    if (frames.size() > depthBefore) {
        run(depthBefore);
    }
    unwind.done = true;
    Val value = std::move(*result);
    for (Val* p = result; p < sp; ++p) *p = Val();
    sp = result;
    return value;
}

Val VM::newInstance(const std::shared_ptr<ClassInfo>& cls) {
    return allocate(cls);
}

void VM::runConstructor(const Val& instance, Args args) {
    if (!instance.isObject() || !instance->cls) {
        return;
    }
    ClassInfo* info = instance->cls.get();
    if (info->constructor.isObject()) {
        callFunction(info->constructor, instance, args);
        return;
    }
    Val replaced;
    runNativeConstructor(info, instance, args, replaced);
}

// Host API

ValuePtr VM::getGlobal(const std::string& name) {
    Val value;
    if (findGlobal(name, value)) {
        return value;
    }
    return Value::makeUndefined();
}

void VM::setGlobal(const std::string& name, ValuePtr value) {
    globals[name] = Val(value);
    ++namesVersion;
}

void VM::registerBuiltin(const std::string& name, NativeFunction func) {
    globals[name] = Val(Value::makeNativeFunction(std::move(func)));
    ++namesVersion;
}

void VM::defineExternClass(const std::string& className, const std::map<std::string, NativeFunction>& methods) {
    ValuePtr classValue = Value::makeClass(className);
    classValue->cls->base = core.object;
    for (const auto& pair : methods) {
        Val method(Value::makeNativeFunction(pair.second));
        classValue->properties[pair.first] = method;
        classValue->cls->prototype[pair.first] = method;
    }
    registerDefinition(className, Val(classValue), true);
}

void VM::registerObject(const std::string& name, const std::string& classType) {
    Val classValue = Val(getGlobal(classType));
    if (!classValue.isClass()) {
        hadError = true;
        errorMessage = "Cannot register object: class '" + classType + "' not found";
        return;
    }
    Val instance = allocate(classValue->cls);
    for (auto& prop : classValue->properties) {
        instance->properties[prop.first] = prop.second;
    }
    globals[name] = instance;
    ++namesVersion;
}

bool VM::looselyEqual(const ValuePtr& a, const ValuePtr& b) {
    return looselyEqual(Val(a), Val(b));
}

bool VM::looselyEqual(const Val& a, const Val& b) {
    using Tag = Val::Tag;
    if (a.isNullish() || b.isNullish()) {
        return a.isNullish() && b.isNullish();
    }
    if (a.tag == b.tag) {
        return a.strictEquals(b);
    }
    // Numbers, strings and booleans as numbers; an object as its primitive
    // value.
    if (a.tag == Tag::Object || b.tag == Tag::Object) {
        if (a.tag == Tag::Object && b.tag == Tag::Object) {
            return a.strictEquals(b);
        }
        const Val& object = a.tag == Tag::Object ? a : b;
        const Val& other = a.tag == Tag::Object ? b : a;
        Val primitive;
        if (object->type == ValueType::Array || object->type == ValueType::Object) {
            primitive = Val::string(object.toString());
        } else {
            return false;
        }
        return looselyEqual(primitive, other);
    }
    if (a.tag == Tag::String && b.tag == Tag::String) {
        return a.str() == b.str();
    }
    return a.toNumber() == b.toNumber();
}

} // namespace as3
