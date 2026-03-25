# Object Method Calls - Not Yet Supported

## Current Status

❌ **Object method calls like `foo.bar()` are NOT yet supported** in the bytecode VM.

## Missing Components

### 1. Function Expressions (Compiler)

**Status:** Not implemented

**Current Code:**
```cpp
void Compiler::compileFunctionExpression(FunctionExpression* expr, Chunk& chunk) {
    // TODO: Similar to compileFunctionDefinition but for expressions
    error("Function expressions not yet implemented in bytecode compiler");
}
```

**Needed For:**
```javascript
var obj = {
    method: function() { return 42; }  // Function expression
};
```

### 2. `this` Binding (Compiler & VM)

**Status:** Returns `undefined`

**Current Code:**
```cpp
} else if (auto* thisExpr = dynamic_cast<ThisExpression*>(expr)) {
    // TODO: Implement this
    emitOpCode(OpCode::OP_UNDEFINED, chunk);
}
```

**Needed For:**
```javascript
var obj = {
    x: 10,
    getX: function() { return this.x; }  // 'this' should refer to obj
};
```

### 3. Method Call Mechanism (VM)

**Status:** No receiver object passing

**Current Behavior:**
When calling `obj.method()`, the current implementation:
1. Pushes `obj` onto the stack
2. Uses `OP_GET_PROPERTY` to get the `method` function
3. Calls `OP_CALL` with the function

**Problem:**
- The function is called without binding `this` to `obj`
- Inside the method, `this` is `undefined`

## What Needs to Be Implemented

### Option A: Implicit Receiver Passing

**Add a new opcode:**
```cpp
case OpCode::OP_CALL_METHOD: {
    uint8_t argCount = readByte();

    // Stack: [... receiver method arg0 arg1 ...]
    ValuePtr callee = peek(argCount);
    ValuePtr receiver = peek(argCount + 1);

    // Store receiver as 'this' for the call
    // ... set up call frame with 'this' binding ...
}
```

**Bytecode for `obj.method(x)`:**
```
OP_GET_LOCAL      0      # obj
OP_DUP                   # Duplicate obj (for receiver)
OP_GET_PROPERTY   'method'
OP_SWAP                  # Swap to: [obj method]
OP_GET_LOCAL      1      # x (argument)
OP_CALL_METHOD    1      # Call with 1 arg + receiver
```

### Option B: Explicit `this` Parameter

**Modify call frames:**
```cpp
struct CallFrame {
    BytecodeFunction* function;
    size_t ip;
    size_t stackOffset;
    ValuePtr thisBinding;  // NEW: Store 'this' value

    CallFrame(BytecodeFunction* func, size_t offset, ValuePtr thisVal = nullptr)
        : function(func), ip(0), stackOffset(offset), thisBinding(thisVal) {}
};
```

**Modify OP_CALL:**
```cpp
case OpCode::OP_CALL: {
    uint8_t argCount = readByte();
    ValuePtr callee = peek(argCount);

    // Check if this is a method call
    // (previous instruction was OP_GET_PROPERTY)
    ValuePtr thisValue = nullptr;
    if (lastOpcodeWasGetProperty) {
        // Receiver object is below the function
        thisValue = peek(argCount + 1);
    }

    // Create call frame with 'this' binding
    frames.emplace_back(func, stackOffset, thisValue);
}
```

**Implement OP_THIS:**
```cpp
case OpCode::OP_THIS: {
    if (!frames.empty()) {
        ValuePtr thisVal = currentFrame().thisBinding;
        push(thisVal ? thisVal : Value::makeUndefined());
    } else {
        push(Value::makeUndefined());
    }
    break;
}
```

### Option C: JavaScript-style Approach

**Pass receiver as implicit first argument:**
- All function calls reserve stack slot 0 for `this`
- Regular function calls: `this` = `undefined`
- Method calls: `this` = receiver object

**Modify function compilation:**
```cpp
// All functions get 'this' as slot 0
current->addLocal("this");  // Reserve slot 0

// Parameters start at slot 1
for (auto& param : stmt->parameters) {
    current->addLocal(param->name->name);
}
```

## Implementation Steps

### Phase 1: Function Expressions
1. Implement `compileFunctionExpression()` in compiler
2. Create function at compile time (like FunctionDefinition)
3. Emit `OP_CLOSURE` to push function onto stack
4. Test: `var f = function() { return 42; }; f();`

### Phase 2: `this` Binding
1. Add `thisBinding` field to CallFrame
2. Implement `OP_THIS` opcode
3. Modify compiler to emit `OP_THIS` for `this` expressions
4. Test: `this` in global scope returns `undefined`

### Phase 3: Method Call Detection
1. Add `OP_CALL_METHOD` or modify `OP_CALL`
2. Track when call target is from property access
3. Pass receiver object as `this` binding
4. Test: `obj.method()` with `this.property` access

### Phase 4: Integration
1. Test method calls with arguments
2. Test nested method calls
3. Test method calls with return values
4. Test `this` in nested functions

## Test Cases Needed

```javascript
// Test 1: Function expression
var f = function() { return 42; };
trace(f());  // Expected: 42

// Test 2: Method with 'this'
var obj = {
    x: 10,
    getX: function() { return this.x; }
};
trace(obj.getX());  // Expected: 10

// Test 3: Method with arguments
var obj = {
    x: 5,
    add: function(n) { return this.x + n; }
};
trace(obj.add(10));  // Expected: 15

// Test 4: Nested method calls
var obj = {
    x: 2,
    double: function() { return this.x * 2; },
    quad: function() { return this.double() * 2; }
};
trace(obj.quad());  // Expected: 8

// Test 5: this in regular function (should be undefined)
function f() { trace(this); }
f();  // Expected: undefined

// Test 6: Method stored in variable
var obj = { x: 10, getX: function() { return this.x; } };
var method = obj.getX;
method();  // Expected: undefined (not bound to obj)
```

## Related Issues

1. **Arrow Functions:** Would need special `this` handling (lexical this)
2. **call/apply/bind:** Would need explicit `this` override
3. **Constructors:** `new` operator would need special `this` creation
4. **Getters/Setters:** Property access with function execution

## Workaround (Current)

Since method calls don't work, you can pass the object as a parameter:

```javascript
// Instead of:
// obj.method()

// Do this:
function method(self) {
    return self.x;
}

var obj = { x: 10 };
trace(method(obj));  // Works: 10
```

## Priority

**Medium-High Priority**

Method calls are a fundamental feature of object-oriented programming. However, the current implementation can still:
- ✅ Call standalone functions
- ✅ Access object properties
- ✅ Pass objects to functions

**Estimated Effort:**
- Function expressions: 2-3 hours
- `this` binding mechanism: 3-4 hours
- Method call integration: 2-3 hours
- Testing and debugging: 2-3 hours
- **Total: ~10-13 hours**

## Conclusion

Object method calls require three major features:
1. Function expressions (to define methods inline)
2. `this` keyword support (to access receiver properties)
3. Receiver binding mechanism (to link `this` to the calling object)

All three components need to be implemented before method calls like `obj.method()` will work properly.
