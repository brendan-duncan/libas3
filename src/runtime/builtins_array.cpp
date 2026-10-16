// Array and Vector.<T>: the same methods over a Value's elements; a vector
// coerces what it is given to its element type, and may be fixed in
// length.

#include "builtins.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace as3 {

namespace {

std::vector<Val>& elementsOf(VM& vm, const Val& self) {
    if (!self.isArray()) {
        vm.throwError("TypeError", "Error #1034: Type Coercion failed: Array expected.", 1034);
    }
    return self->elements;
}

bool isVector(const Val& self) {
    return self.isObject() && self->cls && self->cls->isVector;
}

Coerce elementCoerce(const Val& self) {
    return isVector(self) ? self->cls->elementCoerce : Coerce::None;
}

// What a vector of this type starts its elements as.
Val vectorDefault(Coerce coerce) {
    if (coerce == Coerce::Number) return Val::number(0);
    return defaultForCoerce(coerce);
}

void checkGrowable(VM& vm, const Val& self) {
    if (isVector(self) && self->boolValue) {
        vm.throwError("RangeError", "Error #1126: Cannot change the length of a fixed Vector.", 1126);
    }
}

// A new array, or a vector of the same type as self.
Val sameKind(VM& vm, const Val& self, std::vector<Val> elements) {
    if (isVector(self)) {
        Val made = vm.newInstance(self->cls);
        made->elements = std::move(elements);
        return made;
    }
    return vm.newArray(std::move(elements));
}

// A relative index (negative counts from the end), clamped to [0, length].
size_t clampIndex(double index, size_t length) {
    if (std::isnan(index)) return 0;
    if (index < 0) {
        index += static_cast<double>(length);
        return index < 0 ? 0 : static_cast<size_t>(index);
    }
    return index > static_cast<double>(length) ? length : static_cast<size_t>(index);
}

std::string joinElements(VM& vm, const std::vector<Val>& elements, const std::string& separator) {
    std::string out;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) out += separator;
        if (!elements[i].isNullish()) {
            out += vm.toString(elements[i]);
        }
    }
    return out;
}

// A merge sort: stable, and safe with a comparison that is not consistent.
void mergeSort(std::vector<Val>& items, const std::function<int(const Val&, const Val&)>& compare) {
    if (items.size() < 2) return;
    std::vector<Val> scratch(items.size());
    std::function<void(size_t, size_t)> sortRange = [&](size_t begin, size_t end) {
        if (end - begin < 2) return;
        const size_t middle = begin + (end - begin) / 2;
        sortRange(begin, middle);
        sortRange(middle, end);
        size_t left = begin;
        size_t right = middle;
        size_t out = begin;
        while (left < middle && right < end) {
            if (compare(items[right], items[left]) < 0) {
                scratch[out++] = items[right++];
            } else {
                scratch[out++] = items[left++];
            }
        }
        while (left < middle) scratch[out++] = items[left++];
        while (right < end) scratch[out++] = items[right++];
        for (size_t i = begin; i < end; ++i) items[i] = std::move(scratch[i]);
    };
    sortRange(0, items.size());
}

std::string lowered(const std::string& text) {
    std::string out = text;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

int compareValues(VM& vm, const Val& a, const Val& b, int options) {
    // undefined after everything.
    if (a.isUndefined() || b.isUndefined()) {
        if (a.isUndefined() && b.isUndefined()) return 0;
        return a.isUndefined() ? 1 : -1;
    }
    int result;
    if (options & ArraySort::NUMERIC) {
        const double x = vm.toNumber(a);
        const double y = vm.toNumber(b);
        result = x < y ? -1 : (x > y ? 1 : 0);
    } else {
        std::string x = vm.toString(a);
        std::string y = vm.toString(b);
        if (options & ArraySort::CASEINSENSITIVE) {
            x = lowered(x);
            y = lowered(y);
        }
        result = x < y ? -1 : (x > y ? 1 : 0);
    }
    return (options & ArraySort::DESCENDING) ? -result : result;
}

// Sorts items by a comparison; with the options' UNIQUESORT and
// RETURNINDEXEDARRAY. What sort returns.
Val sortWith(VM& vm, const Val& self, const std::function<int(const Val&, const Val&)>& compare, int options) {
    std::vector<Val>& elements = self->elements;
    if (options & (ArraySort::UNIQUESORT | ArraySort::RETURNINDEXEDARRAY)) {
        // Sorted indices.
        std::vector<Val> indices;
        indices.reserve(elements.size());
        for (size_t i = 0; i < elements.size(); ++i) indices.push_back(Val::number(static_cast<double>(i)));
        const std::vector<Val> copy = elements;
        mergeSort(indices, [&](const Val& a, const Val& b) {
            return compare(copy[static_cast<size_t>(a.num)], copy[static_cast<size_t>(b.num)]);
        });
        if (options & ArraySort::UNIQUESORT) {
            for (size_t i = 1; i < indices.size(); ++i) {
                if (compare(copy[static_cast<size_t>(indices[i - 1].num)], copy[static_cast<size_t>(indices[i].num)]) ==
                    0) {
                    return Val::number(0);
                }
            }
        }
        if (options & ArraySort::RETURNINDEXEDARRAY) {
            return vm.newArray(std::move(indices));
        }
        std::vector<Val> sorted;
        sorted.reserve(copy.size());
        for (const Val& index : indices) sorted.push_back(copy[static_cast<size_t>(index.num)]);
        elements = std::move(sorted);
        return self;
    }
    mergeSort(elements, compare);
    return self;
}

Val sortMethod(VM& vm, const Val& self, Args args) {
    elementsOf(vm, self);
    Val compareFunction;
    int options = 0;
    if (!args.empty()) {
        if (args[0].isFunction()) {
            compareFunction = args[0];
            options = args.integer(1, 0);
        } else {
            options = args.integer(0, 0);
        }
    }
    if (compareFunction.isFunction()) {
        return sortWith(vm, self,
                        [&](const Val& a, const Val& b) {
                            const double r = vm.toNumber(vm.callFunction(compareFunction, Val::null(), {a, b}));
                            const int sign = r < 0 ? -1 : (r > 0 ? 1 : 0);
                            return (options & ArraySort::DESCENDING) ? -sign : sign;
                        },
                        options);
    }
    return sortWith(vm, self, [&](const Val& a, const Val& b) { return compareValues(vm, a, b, options); }, options);
}

Val sortOnMethod(VM& vm, const Val& self, Args args) {
    elementsOf(vm, self);
    std::vector<std::string> names;
    std::vector<int> fieldOptions;
    if (args[0].isArray()) {
        for (const Val& name : args[0]->elements) names.push_back(vm.toString(name));
    } else {
        names.push_back(vm.toString(args[0]));
    }
    int options = 0;
    if (args[1].isArray()) {
        for (const Val& option : args[1]->elements) fieldOptions.push_back(option.toInt32());
        if (!fieldOptions.empty()) options = fieldOptions[0] & (ArraySort::UNIQUESORT | ArraySort::RETURNINDEXEDARRAY);
    } else {
        options = args.integer(1, 0);
    }
    return sortWith(vm, self,
                    [&](const Val& a, const Val& b) {
                        for (size_t i = 0; i < names.size(); ++i) {
                            const int fieldOption = i < fieldOptions.size() ? fieldOptions[i] : options;
                            const Val x = a.isObject() ? vm.getProperty(a, names[i]) : Val();
                            const Val y = b.isObject() ? vm.getProperty(b, names[i]) : Val();
                            const int result = compareValues(vm, x, y, fieldOption);
                            if (result != 0) return result;
                        }
                        return 0;
                    },
                    options);
}

// The methods arrays and vectors share.
void collectionMethods(NativeClassBuilder& builder, bool vector) {
    builder
        .property("length",
                  [](VM& vm, const Val& self, Args) {
                      return Val::number(static_cast<double>(elementsOf(vm, self).size()));
                  },
                  [](VM& vm, const Val& self, Args args) {
                      std::vector<Val>& elements = elementsOf(vm, self);
                      const double length = vm.toNumber(args[0]);
                      if (length < 0 || length != std::floor(length) || length > 4294967295.0) {
                          vm.throwError("RangeError", "Error #1005: Array index is not a positive integer (" +
                                                          numberToString(length) + ").", 1005);
                      }
                      if (static_cast<size_t>(length) != elements.size()) {
                          checkGrowable(vm, self);
                      }
                      elements.resize(static_cast<size_t>(length), vectorDefault(elementCoerce(self)));
                      return Val();
                  })
        .method("push",
                [](VM& vm, const Val& self, Args args) {
                    std::vector<Val>& elements = elementsOf(vm, self);
                    if (!args.empty()) checkGrowable(vm, self);
                    const Coerce coerce = elementCoerce(self);
                    for (const Val& arg : args) elements.push_back(coerceValue(arg, coerce));
                    return Val::number(static_cast<double>(elements.size()));
                },
                1)
        .method("pop",
                [](VM& vm, const Val& self, Args) {
                    std::vector<Val>& elements = elementsOf(vm, self);
                    if (elements.empty()) return isVector(self) ? vectorDefault(elementCoerce(self)) : Val();
                    checkGrowable(vm, self);
                    Val last = std::move(elements.back());
                    elements.pop_back();
                    return last;
                })
        .method("shift",
                [](VM& vm, const Val& self, Args) {
                    std::vector<Val>& elements = elementsOf(vm, self);
                    if (elements.empty()) return isVector(self) ? vectorDefault(elementCoerce(self)) : Val();
                    checkGrowable(vm, self);
                    Val first = std::move(elements.front());
                    elements.erase(elements.begin());
                    return first;
                })
        .method("unshift",
                [](VM& vm, const Val& self, Args args) {
                    std::vector<Val>& elements = elementsOf(vm, self);
                    if (!args.empty()) checkGrowable(vm, self);
                    const Coerce coerce = elementCoerce(self);
                    std::vector<Val> added;
                    for (const Val& arg : args) added.push_back(coerceValue(arg, coerce));
                    elements.insert(elements.begin(), added.begin(), added.end());
                    return Val::number(static_cast<double>(elements.size()));
                },
                1)
        .method("splice",
                [](VM& vm, const Val& self, Args args) {
                    std::vector<Val>& elements = elementsOf(vm, self);
                    if (args.empty()) return sameKind(vm, self, {});
                    const size_t length = elements.size();
                    const size_t start = clampIndex(vm.toNumber(args[0]), length);
                    size_t count = length - start;
                    if (args.size() > 1) {
                        const double asked = vm.toNumber(args[1]);
                        count = asked < 0 || std::isnan(asked) ? 0 : std::min(static_cast<size_t>(asked), length - start);
                    }
                    const size_t added = args.size() > 2 ? args.size() - 2 : 0;
                    if (count != added) checkGrowable(vm, self);
                    std::vector<Val> removed(elements.begin() + static_cast<long>(start),
                                             elements.begin() + static_cast<long>(start + count));
                    elements.erase(elements.begin() + static_cast<long>(start),
                                   elements.begin() + static_cast<long>(start + count));
                    const Coerce coerce = elementCoerce(self);
                    std::vector<Val> inserted;
                    for (size_t i = 2; i < args.size(); ++i) inserted.push_back(coerceValue(args[i], coerce));
                    elements.insert(elements.begin() + static_cast<long>(start), inserted.begin(), inserted.end());
                    return sameKind(vm, self, std::move(removed));
                },
                2)
        .method("slice",
                [](VM& vm, const Val& self, Args args) {
                    std::vector<Val>& elements = elementsOf(vm, self);
                    const size_t length = elements.size();
                    const size_t start = clampIndex(args.empty() ? 0 : vm.toNumber(args[0]), length);
                    const size_t end =
                        args.size() > 1 && !args[1].isUndefined() ? clampIndex(vm.toNumber(args[1]), length) : length;
                    std::vector<Val> part;
                    if (end > start) {
                        part.assign(elements.begin() + static_cast<long>(start), elements.begin() + static_cast<long>(end));
                    }
                    return sameKind(vm, self, std::move(part));
                },
                2)
        .method("concat",
                [](VM& vm, const Val& self, Args args) {
                    std::vector<Val> joined = elementsOf(vm, self);
                    const Coerce coerce = elementCoerce(self);
                    for (const Val& arg : args) {
                        if (arg.isArray()) {
                            for (const Val& element : arg->elements) joined.push_back(coerceValue(element, coerce));
                        } else {
                            joined.push_back(coerceValue(arg, coerce));
                        }
                    }
                    return sameKind(vm, self, std::move(joined));
                },
                1)
        .method("join",
                [](VM& vm, const Val& self, Args args) {
                    const std::string separator = args.empty() || args[0].isUndefined() ? "," : vm.toString(args[0]);
                    return Val::string(joinElements(vm, elementsOf(vm, self), separator));
                },
                1)
        .method("toString",
                [](VM& vm, const Val& self, Args) { return Val::string(joinElements(vm, elementsOf(vm, self), ",")); })
        .method("toLocaleString",
                [](VM& vm, const Val& self, Args) { return Val::string(joinElements(vm, elementsOf(vm, self), ",")); })
        .method("reverse",
                [](VM& vm, const Val& self, Args) {
                    std::vector<Val>& elements = elementsOf(vm, self);
                    std::reverse(elements.begin(), elements.end());
                    return self;
                })
        .method("indexOf",
                [](VM& vm, const Val& self, Args args) {
                    std::vector<Val>& elements = elementsOf(vm, self);
                    const size_t from = clampIndex(args.size() > 1 ? vm.toNumber(args[1]) : 0, elements.size());
                    for (size_t i = from; i < elements.size(); ++i) {
                        if (elements[i].strictEquals(args[0])) return Val::number(static_cast<double>(i));
                    }
                    return Val::number(-1);
                },
                1)
        .method("lastIndexOf",
                [](VM& vm, const Val& self, Args args) {
                    std::vector<Val>& elements = elementsOf(vm, self);
                    if (elements.empty()) return Val::number(-1);
                    double from = args.size() > 1 ? vm.toNumber(args[1]) : 2147483647.0;
                    if (from < 0) from += static_cast<double>(elements.size());
                    if (from < 0) return Val::number(-1);
                    size_t start = std::min(static_cast<size_t>(from), elements.size() - 1);
                    for (size_t i = start + 1; i-- > 0;) {
                        if (elements[i].strictEquals(args[0])) return Val::number(static_cast<double>(i));
                    }
                    return Val::number(-1);
                },
                1)
        .method("forEach",
                [](VM& vm, const Val& self, Args args) {
                    elementsOf(vm, self);
                    for (size_t i = 0; i < self->elements.size(); ++i) {
                        const Val element = self->elements[i];
                        vm.callFunction(args[0], args[1], {element, Val::number(static_cast<double>(i)), self});
                    }
                    return Val();
                },
                1)
        .method("map",
                [](VM& vm, const Val& self, Args args) {
                    elementsOf(vm, self);
                    std::vector<Val> mapped;
                    const Coerce coerce = elementCoerce(self);
                    for (size_t i = 0; i < self->elements.size(); ++i) {
                        const Val element = self->elements[i];
                        mapped.push_back(coerceValue(
                            vm.callFunction(args[0], args[1], {element, Val::number(static_cast<double>(i)), self}),
                            coerce));
                    }
                    return sameKind(vm, self, std::move(mapped));
                },
                1)
        .method("filter",
                [](VM& vm, const Val& self, Args args) {
                    elementsOf(vm, self);
                    std::vector<Val> kept;
                    for (size_t i = 0; i < self->elements.size(); ++i) {
                        const Val element = self->elements[i];
                        if (vm.callFunction(args[0], args[1], {element, Val::number(static_cast<double>(i)), self})
                                .toBoolean()) {
                            kept.push_back(element);
                        }
                    }
                    return sameKind(vm, self, std::move(kept));
                },
                1)
        .method("some",
                [](VM& vm, const Val& self, Args args) {
                    elementsOf(vm, self);
                    for (size_t i = 0; i < self->elements.size(); ++i) {
                        const Val element = self->elements[i];
                        if (vm.callFunction(args[0], args[1], {element, Val::number(static_cast<double>(i)), self})
                                .toBoolean()) {
                            return Val::boolean(true);
                        }
                    }
                    return Val::boolean(false);
                },
                1)
        .method("every",
                [](VM& vm, const Val& self, Args args) {
                    elementsOf(vm, self);
                    for (size_t i = 0; i < self->elements.size(); ++i) {
                        const Val element = self->elements[i];
                        if (!vm.callFunction(args[0], args[1], {element, Val::number(static_cast<double>(i)), self})
                                 .toBoolean()) {
                            return Val::boolean(false);
                        }
                    }
                    return Val::boolean(true);
                },
                1)
        .method("sort", sortMethod, 1)
        .method("sortOn", sortOnMethod, 2);
    (void)vector;
}

} // namespace

void defineArrayBuiltins(VM& vm) {
    VM::Builtins& core = vm.builtins();

    const auto makeArray = [](VM& vm, const Val& self, Args args) -> Val {
        std::vector<Val>& elements = self->elements;
        if (args.size() == 1 && args[0].isNumber()) {
            const double length = args[0].num;
            if (length < 0 || length != std::floor(length) || length > 4294967295.0) {
                vm.throwError("RangeError",
                              "Error #1005: Array index is not a positive integer (" + numberToString(length) + ").",
                              1005);
            }
            elements.assign(static_cast<size_t>(length), Val());
        } else {
            elements.assign(args.begin(), args.end());
        }
        return Val();
    };
    NativeClassBuilder array = vm.defineNativeClass("Array");
    array.info().instanceType = ValueType::Array;
    array.dynamic()
        .constant("CASEINSENSITIVE", Val::number(ArraySort::CASEINSENSITIVE))
        .constant("DESCENDING", Val::number(ArraySort::DESCENDING))
        .constant("UNIQUESORT", Val::number(ArraySort::UNIQUESORT))
        .constant("RETURNINDEXEDARRAY", Val::number(ArraySort::RETURNINDEXEDARRAY))
        .constant("NUMERIC", Val::number(ArraySort::NUMERIC))
        .constructor(makeArray)
        .callHandler([makeArray](VM& vm, const Val&, Args args) {
            Val made = vm.newArray();
            makeArray(vm, made, args);
            return made;
        });
    collectionMethods(array, false);
    core.array = array.define().ref->cls;

    NativeClassBuilder vector = vm.defineNativeClass("__AS3__.vec.Vector");
    vector.info().instanceType = ValueType::Array;
    vector.info().isVector = true;
    vector.final()
        .constructor([](VM& vm, const Val& self, Args args) {
            const double length = args.empty() ? 0 : vm.toNumber(args[0]);
            if (length < 0 || length != std::floor(length) || length > 4294967295.0) {
                vm.throwError("RangeError",
                              "Error #1005: Array index is not a positive integer (" + numberToString(length) + ").",
                              1005);
            }
            self->elements.assign(static_cast<size_t>(length), vectorDefault(elementCoerce(self)));
            self->boolValue = args.boolean(1, false);
            return Val();
        })
        .callHandler([](VM& vm, const Val& cls, Args args) {
            // Vector.<T>(list): a vector of the list's elements.
            if (!args[0].isArray()) {
                vm.throwError("TypeError", "Error #1034: Type Coercion failed: cannot convert " +
                                               vm.toString(args[0]) + " to " + cls->cls->name + ".", 1034);
            }
            Val made = vm.newInstance(cls->cls);
            for (const Val& element : args[0]->elements) {
                made->elements.push_back(coerceValue(element, cls->cls->elementCoerce));
            }
            return made;
        })
        .property("fixed", [](VM&, const Val& self, Args) { return Val::boolean(self->boolValue); },
                  [](VM&, const Val& self, Args args) {
                      self->boolValue = args[0].toBoolean();
                      return Val();
                  });
    collectionMethods(vector, true);
    Val vectorClass = vector.define();
    core.vector = vectorClass->cls;
    // "Vector" is the generic one, Vector.<T> made from it.
    vm.defineValue("Vector", vectorClass);
}

} // namespace as3
