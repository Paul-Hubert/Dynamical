#include "expr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace dy {

class ExprParser {
public:
    ExprParser(const std::string& text, const std::vector<std::string>& names, Expression& out) : text(text), names(names), out(out) {}

    void run() {
        comparison();
        skip();
        if(at < text.size()) fail("unexpected '" + std::string(1, text[at]) + "'");
        // The stack the postfix code needs
        int depth = 0, most = 0;
        for(auto& i : out.code) {
            depth += effect(i.op);
            most = std::max(most, depth);
        }
        out.depth = most;
        std::sort(out.used.begin(), out.used.end());
        out.used.erase(std::unique(out.used.begin(), out.used.end()), out.used.end());
    }

private:
    using Op = Expression::Op;

    static int effect(Op op) {
        switch(op) {
        case Op::Number: case Op::Signal: return 1;
        case Op::Neg: case Op::Abs: case Op::Exp: case Op::Log: case Op::Sqrt: case Op::Sign: case Op::Step: return 0;
        case Op::Clamp: case Op::If: return -2;
        default: return -1;
        }
    }

    [[noreturn]] void fail(const std::string& what) const {
        throw ExprError("'" + text + "': " + what + " at character " + std::to_string(at + 1));
    }

    void skip() {
        while(at < text.size() && std::isspace((unsigned char) text[at])) at++;
    }

    bool take(const char* token) {
        skip();
        size_t n = 0;
        while(token[n]) n++;
        if(text.compare(at, n, token) != 0) return false;
        at += n;
        return true;
    }

    void emit(Op op) {
        out.code.push_back({ op, 0.f, 0 });
    }

    void comparison() {
        sum();
        while(true) {
            Op op;
            // Two character operators first
            if(take("<=")) op = Op::LessEqual;
            else if(take(">=")) op = Op::GreaterEqual;
            else if(take("==")) op = Op::Equal;
            else if(take("!=")) op = Op::NotEqual;
            else if(take("<")) op = Op::Less;
            else if(take(">")) op = Op::Greater;
            else return;
            sum();
            emit(op);
        }
    }

    void sum() {
        product();
        while(true) {
            if(take("+")) {
                product();
                emit(Op::Add);
            } else if(take("-")) {
                product();
                emit(Op::Sub);
            } else return;
        }
    }

    void product() {
        unary();
        while(true) {
            if(take("*")) {
                unary();
                emit(Op::Mul);
            } else if(take("/")) {
                unary();
                emit(Op::Div);
            } else return;
        }
    }

    void unary() {
        if(take("-")) {
            unary();
            emit(Op::Neg);
        } else if(take("+")) {
            unary();
        } else {
            primary();
        }
    }

    void arguments(int count, const std::string& name) {
        if(!take("(")) fail(name + " needs (");
        for(int i = 0; i < count; i++) {
            if(i > 0 && !take(",")) fail(name + " takes " + std::to_string(count) + " arguments");
            comparison();
        }
        if(!take(")")) fail(name + " takes " + std::to_string(count) + " arguments, expected )");
    }

    void primary() {
        skip();
        if(at >= text.size()) fail("unexpected end");
        const char c = text[at];
        if(c == '(') {
            at++;
            comparison();
            if(!take(")")) fail("expected )");
            return;
        }
        if(std::isdigit((unsigned char) c) || c == '.') {
            const char* begin = text.c_str() + at;
            char* end = nullptr;
            float value = std::strtof(begin, &end);
            if(end == begin) fail("not a number");
            at += (size_t) (end - begin);
            out.code.push_back({ Op::Number, value, 0 });
            return;
        }
        if(std::isalpha((unsigned char) c) || c == '_') {
            size_t begin = at;
            while(at < text.size() && (std::isalnum((unsigned char) text[at]) || text[at] == '_')) at++;
            const std::string name = text.substr(begin, at - begin);
            struct Function { const char* name; Op op; int arguments; };
            static const Function functions[] = {
                { "min", Op::Min, 2 }, { "max", Op::Max, 2 }, { "abs", Op::Abs, 1 }, { "exp", Op::Exp, 1 }, { "log", Op::Log, 1 }, { "sqrt", Op::Sqrt, 1 },
                { "pow", Op::Pow, 2 }, { "sign", Op::Sign, 1 }, { "step", Op::Step, 1 }, { "clamp", Op::Clamp, 3 }, { "if", Op::If, 3 } };
            for(const Function& f : functions) {
                if(name != f.name) continue;
                arguments(f.arguments, name);
                emit(f.op);
                return;
            }
            auto it = std::find(names.begin(), names.end(), name);
            if(it == names.end()) {
                at = begin;
                fail("unknown signal or function '" + name + "'");
            }
            int index = (int) (it - names.begin());
            out.code.push_back({ Op::Signal, 0.f, index });
            out.used.push_back(index);
            return;
        }
        fail("unexpected '" + std::string(1, c) + "'");
    }

    const std::string& text;
    const std::vector<std::string>& names;
    Expression& out;
    size_t at = 0;
};

Expression Expression::parse(const std::string& text, const std::vector<std::string>& names) {
    Expression e;
    e.source = text;
    ExprParser(text, names, e).run();
    return e;
}

float Expression::eval(const float* values) const {
    if(code.empty()) return 0.f;
    float small[32];
    std::vector<float> large;
    float* stack = small;
    if(depth > 32) {
        large.resize((size_t) depth);
        stack = large.data();
    }
    int n = 0;
    auto finite = [](float v) { return std::isfinite(v) ? v : 0.f; };
    for(const Instruction& i : code) {
        switch(i.op) {
        case Op::Number: stack[n++] = i.number; break;
        case Op::Signal: stack[n++] = finite(values[i.signal]); break;
        case Op::Add: n--; stack[n - 1] += stack[n]; break;
        case Op::Sub: n--; stack[n - 1] -= stack[n]; break;
        case Op::Mul: n--; stack[n - 1] *= stack[n]; break;
        case Op::Div: n--; stack[n - 1] = stack[n] != 0.f ? stack[n - 1] / stack[n] : 0.f; break;
        case Op::Neg: stack[n - 1] = -stack[n - 1]; break;
        case Op::Less: n--; stack[n - 1] = stack[n - 1] < stack[n] ? 1.f : 0.f; break;
        case Op::Greater: n--; stack[n - 1] = stack[n - 1] > stack[n] ? 1.f : 0.f; break;
        case Op::LessEqual: n--; stack[n - 1] = stack[n - 1] <= stack[n] ? 1.f : 0.f; break;
        case Op::GreaterEqual: n--; stack[n - 1] = stack[n - 1] >= stack[n] ? 1.f : 0.f; break;
        case Op::Equal: n--; stack[n - 1] = stack[n - 1] == stack[n] ? 1.f : 0.f; break;
        case Op::NotEqual: n--; stack[n - 1] = stack[n - 1] != stack[n] ? 1.f : 0.f; break;
        case Op::Min: n--; stack[n - 1] = std::min(stack[n - 1], stack[n]); break;
        case Op::Max: n--; stack[n - 1] = std::max(stack[n - 1], stack[n]); break;
        case Op::Abs: stack[n - 1] = std::abs(stack[n - 1]); break;
        case Op::Exp: stack[n - 1] = finite(std::exp(stack[n - 1])); break;
        case Op::Log: stack[n - 1] = stack[n - 1] > 0.f ? std::log(stack[n - 1]) : 0.f; break;
        case Op::Sqrt: stack[n - 1] = stack[n - 1] > 0.f ? std::sqrt(stack[n - 1]) : 0.f; break;
        case Op::Pow: n--; stack[n - 1] = finite(std::pow(stack[n - 1], stack[n])); break;
        case Op::Sign: stack[n - 1] = stack[n - 1] > 0.f ? 1.f : stack[n - 1] < 0.f ? -1.f : 0.f; break;
        case Op::Step: stack[n - 1] = stack[n - 1] > 0.f ? 1.f : 0.f; break;
        case Op::Clamp: n -= 2; stack[n - 1] = std::min(std::max(stack[n - 1], stack[n]), stack[n + 1]); break;
        case Op::If: n -= 2; stack[n - 1] = stack[n - 1] != 0.f ? stack[n] : stack[n + 1]; break;
        }
    }
    return finite(stack[0]);
}

}
