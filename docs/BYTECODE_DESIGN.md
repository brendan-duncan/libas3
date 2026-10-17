# Bytecode VM Design

## Overview

A **stack-based virtual machine** for ActionScript 3. The parser makes an AST; the compiler turns each function into bytecode (and each class into a template); the VM runs it.

```
┌────────────┐     ┌──────────────┐     ┌────────────┐
│   Parser   │────>│   Compiler   │────>│     VM     │
│    (AST)   │     │(AST→Bytecode)│     │  (Execute) │
└────────────┘     └──────────────┘     └────────────┘
```

## Values

`Val` (runtime/value.h) is what the stack, locals, fields and arrays hold: a tag, a number or boolean in place, and a `ValueRef` for a string or an object. Copying a number touches no reference count; copying a string or an object counts it on the `Value`, without atomics. While any `Val` holds a `Value`, the `Value` holds a `std::shared_ptr` to itself, so the host's `ValuePtr`s and `weak_ptr`s see it held as before.

`Value` is the heap object: a string's text, an object's dynamic properties (`PropertyMap`, made on first use), an array's elements, an instance's fields (`slots`), a function's `FunctionInfo`, a class's `ClassInfo`, and the host's `HostObject`. Older host code uses `ValuePtr` (`std::shared_ptr<Value>`); `Val` converts to and from it.

## Classes

A `ClassInfo` has the class's qualified name, its superclass and interfaces, and its members as `Trait`s:
- instance traits, flattened (a subclass starts with a copy of its superclass's and overrides by name): a field's slot, a method's function, an accessor's getter and setter;
- static traits, the class's own, with their slots on the class;
- prototype methods, found after an object's dynamic properties.

A script class is compiled to a `ClassTemplate` (members, a constructor function that runs the field initializers and then the body, a static initializer); `OP_DEFINE_CLASS` makes the `ClassInfo` when it runs, resolving the superclass by name then (asking the class resolver if needed). Native classes are made by `NativeClassBuilder` into the same `ClassInfo`, so the two mix freely.

## Functions and calls

Each function is a `BytecodeFunction`: its code, constants and names, its locals (parameters first) and captured variables, its exception handlers and its inline caches.

A call pushes a `CallFrame`: where its locals start on the stack (the arguments become the first locals in place), where the result goes, `this`, the instance its names find (for a method, `this`; for a function made in a method, that method's instance), the timeline scope (frame scripts), and the class it belongs to (for `super` and statics). Frames are made once and used again.

Variables are function-scoped. A local that a closure uses is a `Cell`, shared by the function and its closures (so every closure made in a loop sees the one variable, as in ActionScript); the compiler works out which from the names the nested functions use.

A name that is not a local, a captured variable or a member of the class being compiled is looked up when it runs (`OP_GET_NAME`): on the instance (inherited and native members), the class's statics and its superclasses', the timeline, the imports, the package, the globals, then the wildcard imports and the class resolver. What is found is cached at the instruction until a global or a definition is added (in any VM: the caches are in the code, which more than one VM may run); a global by its slot, so that giving it a new value keeps the cache.

## Inline caches

`OP_GET_PROP`, `OP_SET_PROP`, `OP_CALL_PROP` and their fused forms (`OP_GET_THIS_PROP`, `OP_GET_LOCAL_PROP`, `OP_SET_THIS_PROP`, `OP_SET_LOCAL_PROP`, `OP_CALL_THIS_PROP`, `OP_CALL_LOCAL_PROP`) keep the class last seen and its trait. When the object is of that class again, a field is read or written by its slot, a getter, setter or method runs at once (a script one in a frame of the same loop, a native one directly), with no lookup by name. The fused forms read the object from a local or the instance without putting it on the stack.

## Exceptions

`throw` and runtime errors raise a C++ `ScriptException` holding the value. The loop catches it and looks for a handler in the frames, innermost first: each function's handlers are ranges of its code with where to go. `finally` blocks are compiled into each way out of the `try` (falling out, `break`, `continue`, `return`), outside its handler's range, and into a handler that runs them and throws again. A native function calling a script (sort's compare function, a listener) runs a nested loop; an exception not handled there goes on through the native to the outer loop.

## Instruction Set

Operands follow the opcode: u8, u16 (indices) and s32 (jumps, relative to the end of the instruction).

```
Constants      CONSTANT, UNDEFINED, NULL, TRUE, FALSE, ZERO, ONE
Stack          POP, DUP, DUP2, SWAP, DUP_X1, DUP_X2
Locals         GET_LOCAL, SET_LOCAL, PUT_LOCAL, INC_LOCAL
Captured       GET_CELL, SET_CELL, PUT_CELL, GET_UPVALUE, SET_UPVALUE, PUT_UPVALUE
Names          GET_NAME, SET_NAME, TYPEOF_NAME, DEFINE_VAR, DEFINE_FUNCTION, GET_QNAME
Properties     GET_PROP, SET_PROP, DELETE_PROP, GET_INDEX, SET_INDEX, DELETE_INDEX,
               GET_SUPER, SET_SUPER, GET_THIS_PROP, SET_THIS_PROP, GET_LOCAL_PROP, SET_LOCAL_PROP
Arithmetic     ADD, SUBTRACT, MULTIPLY, DIVIDE, MODULO, NEGATE, TO_NUMBER, INCREMENT, DECREMENT
Comparison     EQUAL, NOT_EQUAL, STRICT_EQUAL, STRICT_NOT_EQUAL, LESS, LESS_EQUAL, GREATER, GREATER_EQUAL
Logic, bits    NOT, BIT_AND, BIT_OR, BIT_XOR, BIT_NOT, LEFT_SHIFT, RIGHT_SHIFT, UNSIGNED_RIGHT_SHIFT
Types          TYPEOF, INSTANCEOF, IN, IS, AS, COERCE (to int, uint, Number, String, Boolean, or null)
Jumps          JUMP, JUMP_IF_FALSE, JUMP_IF_TRUE, and the _KEEP forms for && || ??
Calls          CALL, CALL_PROP, CALL_NAME, CALL_INDEX, CALL_SUPER, CALL_THIS_PROP, CALL_LOCAL_PROP,
               SUPER_CTOR, NEW, RETURN, RETURN_UNDEFINED, DEFAULT_ARG
Context        THIS, SCOPE_THIS, OWNER_CLASS, CLOSURE
Making         NEW_ARRAY, NEW_OBJECT, NEW_VECTOR, NEW_REGEXP, APPLY_TYPE, DEFINE_CLASS
Other          THROW, ITER_INIT, ITER_NEXT, NOP
Fused          GET_LOCAL_CONSTANT, GET_LOCAL_LOCAL, COERCE_PUT_LOCAL, SET_LOCAL_PROP_POP, SET_THIS_PROP_POP,
               LESS_JUMP, GREATER_JUMP, LESS_EQUAL_JUMP, GREATER_EQUAL_JUMP, EQUAL_JUMP, NOT_EQUAL_JUMP,
               STRICT_EQUAL_JUMP, STRICT_NOT_EQUAL_JUMP
```

`int` and `uint` are not types of values: numbers are doubles, and `COERCE` (or `INC_LOCAL`'s coercion, or a field's trait) wraps what is stored where a type is declared.

### Fused instructions

When a function's code is complete, `Chunk::fuse` looks for pairs of instructions that often run together and writes a fused opcode over the first of each: a local and a constant pushed (`GET_LOCAL_CONSTANT`), a comparison and the jump after it (`LESS_JUMP`), a coercion and the store after it (`COERCE_PUT_LOCAL`), a property set as a statement and its `POP`. The fused instruction runs both, so the loop dispatches once instead of twice. Only the first opcode byte changes: the second instruction stays whole after it, so a jump to it still finds it, and no offset, line or handler range moves. Whatever can throw is in the first instruction of a pair, so an error is reported where it was before.

### Example

```actionscript
class Particle {
    public var x:Number = 0;
    public function step(dx:Number):Number { x += dx; return x; }
}
function loop():void {
    var p:Particle = new Particle();
    for (var i:int = 0; i < 1000; i++) p.step(0.25);
}
```

`step` and `loop` compile to:

```
== step ==
0000    3 GET_THIS_PROP             'x'
0005    | GET_LOCAL                 0
0008    | ADD
0009    | SET_THIS_PROP_POP         'x'
0014    | POP
0015    | GET_THIS_PROP             'x'
0020    | COERCE                    3
0022    | RETURN
0023    | RETURN_UNDEFINED
== loop ==
0000    6 GET_NAME                  'Particle'
0005    | NEW                       0
0007    | COERCE_PUT_LOCAL          6
0009    | PUT_LOCAL                 0
0012    7 ZERO
0013    | COERCE_PUT_LOCAL          1
0015    | PUT_LOCAL                 1
0018    | JUMP                      -> 41
0023    | UNDEFINED
0024    | CONSTANT                  0 '0.25'
0027    | CALL_LOCAL_PROP           local 0 'step'
0035    | POP
0036    | INC_LOCAL                 1
0041    | GET_LOCAL_CONSTANT        1
0044    | CONSTANT                  1 '1000'
0047    | LESS_JUMP
0048    | JUMP_IF_TRUE              -> 23
0053    | RETURN_UNDEFINED
```

The `UNDEFINED` before the argument is the place `CALL_LOCAL_PROP`'s result goes (the object stays in its local). The loop's test is at its bottom, after a jump to it the first time. A fused instruction is listed with the second of its pair after it, which it runs too: the loop's test (`GET_LOCAL_CONSTANT`, `LESS_JUMP`) is two dispatches, not four.
