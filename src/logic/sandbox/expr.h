#ifndef DY_SANDBOX_EXPR_H
#define DY_SANDBOX_EXPR_H

#include <stdexcept>
#include <string>
#include <vector>

namespace dy {

// The expressions of the reward files (docs/action-agents.md, rewards): numbers, named signals, + - * /, unary minus,
// comparisons (1 or 0), parentheses, and the functions min, max, abs, exp, log, sqrt, pow, sign, step (1 above zero),
// clamp(x, lo, hi), if(condition, then, else). Compiled once against the list of signal names, evaluated on their
// values. A division by zero and anything that is not finite give 0

struct ExprError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Expression {
public:
    Expression() = default;

    // names: the signals, in the order eval() gets their values. Throws ExprError with the position of the fault
    static Expression parse(const std::string& text, const std::vector<std::string>& names);

    float eval(const float* values) const;

    const std::string& text() const { return source; }
    // Which signals the expression reads (indices in `names`)
    const std::vector<int>& uses() const { return used; }

private:
    enum class Op : unsigned char { Number, Signal, Add, Sub, Mul, Div, Neg, Less, Greater, LessEqual, GreaterEqual, Equal, NotEqual,
        Min, Max, Abs, Exp, Log, Sqrt, Pow, Sign, Step, Clamp, If };
    struct Instruction {
        Op op;
        float number = 0.f;
        int signal = 0;
    };

    friend class ExprParser;
    std::string source;
    std::vector<Instruction> code;      // postfix
    std::vector<int> used;
    int depth = 0;                      // of the stack it needs
};

}

#endif
