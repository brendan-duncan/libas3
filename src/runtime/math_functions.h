#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace as3 {

// Math's functions on numbers, shared by the Math class and the VM's
// CALL_MATH (Math.sqrt(x) called without a call when Math is Math and the
// arguments are numbers), so that both give the same.
enum class MathFunction : uint8_t {
    None,
    Abs,
    Acos,
    Asin,
    Atan,
    Ceil,
    Cos,
    Exp,
    Floor,
    Log,
    Round,
    Sin,
    Sqrt,
    Tan,
    Atan2,
    Pow,
    Max,
    Min,
};

// The function a member of Math names, None for any other name.
MathFunction mathFunctionNamed(const std::string& name);

// Whether CALL_MATH takes a call of it with this many arguments: one for
// the unary ones, two for atan2 and pow, a few for max and min.
inline bool mathTakes(MathFunction f, size_t argc) {
    switch (f) {
        case MathFunction::None:
            return false;
        case MathFunction::Atan2:
        case MathFunction::Pow:
            return argc == 2;
        case MathFunction::Max:
        case MathFunction::Min:
            return argc <= 8;
        default:
            return argc == 1;
    }
}

inline double mathUnary(MathFunction f, double x) {
    switch (f) {
        case MathFunction::Abs: return std::fabs(x);
        case MathFunction::Acos: return std::acos(x);
        case MathFunction::Asin: return std::asin(x);
        case MathFunction::Atan: return std::atan(x);
        case MathFunction::Ceil: return std::ceil(x);
        case MathFunction::Cos: return std::cos(x);
        case MathFunction::Exp: return std::exp(x);
        case MathFunction::Floor: return std::floor(x);
        case MathFunction::Log: return std::log(x);
        case MathFunction::Round:
            if (std::isnan(x) || std::isinf(x)) return x;
            return std::floor(x + 0.5);
        case MathFunction::Sin: return std::sin(x);
        case MathFunction::Sqrt: return std::sqrt(x);
        case MathFunction::Tan: return std::tan(x);
        default: return std::numeric_limits<double>::quiet_NaN();
    }
}

inline double mathPow(double x, double y) {
    if (std::isnan(y)) return std::numeric_limits<double>::quiet_NaN();
    if (y == 0) return 1;
    if ((x == 1 || x == -1) && std::isinf(y)) return std::numeric_limits<double>::quiet_NaN();
    return std::pow(x, y);
}

// Math.max and Math.min of numbers: NaN when any is; -0 below 0.
inline double mathMax(const double* xs, size_t count) {
    double best = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < count; ++i) {
        const double x = xs[i];
        if (std::isnan(x)) return std::numeric_limits<double>::quiet_NaN();
        if (x > best || (x == 0 && best == 0 && !std::signbit(x))) best = x;
    }
    return best;
}

inline double mathMin(const double* xs, size_t count) {
    double best = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < count; ++i) {
        const double x = xs[i];
        if (std::isnan(x)) return std::numeric_limits<double>::quiet_NaN();
        if (x < best || (x == 0 && best == 0 && std::signbit(x))) best = x;
    }
    return best;
}

// The function of numbers, as many as mathTakes allows.
inline double mathCall(MathFunction f, const double* xs, size_t count) {
    switch (f) {
        case MathFunction::Atan2: return std::atan2(xs[0], xs[1]);
        case MathFunction::Pow: return mathPow(xs[0], xs[1]);
        case MathFunction::Max: return mathMax(xs, count);
        case MathFunction::Min: return mathMin(xs, count);
        default: return mathUnary(f, xs[0]);
    }
}

} // namespace as3
