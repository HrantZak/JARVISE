#pragma once
#include <QString>
#include <QStringList>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>

namespace jarvis::scene3d {
// Bounded arithmetic grammar; no JavaScript, shell, file or network access.
class Expression {
    struct Op { QString name; double value{}; };
    std::vector<Op> code;
public:
    explicit Expression(QString source) {
        if (source.size() > 256) throw std::runtime_error("Formula is too long");
        source = source.toLower(); source.remove(' ');
        int pos = 0, depth = 0;
        auto appendOp = [&](QString name, double value = 0) {
            if (code.size() >= 128) throw std::runtime_error("Formula is too complex");
            code.push_back({name, value});
        };
        auto take = [&](QChar c) { if (pos < source.size() && source[pos] == c) { ++pos; return true; } return false; };
        std::function<void()> sum, product, unary, power, atom;
        atom = [&] {
            if (++depth > 24) throw std::runtime_error("Formula nesting is too deep");
            if (take('(')) { sum(); if (!take(')')) throw std::runtime_error("Expected closing parenthesis"); }
            else if (pos < source.size() && (source[pos].isDigit() || source[pos] == '.')) {
                int start = pos;
                while (pos < source.size() && (source[pos].isDigit() || source[pos] == '.')) ++pos;
                bool ok; const double value = source.mid(start, pos-start).toDouble(&ok);
                if (!ok || !std::isfinite(value)) throw std::runtime_error("Invalid number");
                appendOp("number", value);
            } else {
                int start = pos;
                while (pos < source.size() && source[pos].isLetter()) ++pos;
                const auto word = source.mid(start, pos-start);
                if (word == "x" || word == "y") appendOp(word);
                else if (word == "pi") appendOp("number", 3.141592653589793);
                else if (word == "e") appendOp("number", std::exp(1.0));
                else if (QStringList{"sin","cos","tan","sqrt","abs","exp","log"}.contains(word)) {
                    if (!take('(')) throw std::runtime_error("Expected function argument");
                    sum(); if (!take(')')) throw std::runtime_error("Expected closing parenthesis");
                    appendOp(word);
                } else throw std::runtime_error("Unsupported formula token");
            }
            --depth;
        };
        power = [&] { atom(); if (take('^')) { unary(); appendOp("^"); } };
        unary = [&] {
            if (++depth > 24) throw std::runtime_error("Formula nesting is too deep");
            if (take('-')) { unary(); appendOp("neg"); } else if (take('+')) unary(); else power();
            --depth;
        };
        product = [&] { unary(); while (pos < source.size() && (source[pos] == '*' || source[pos] == '/')) { const auto op=source.mid(pos++,1); unary(); appendOp(op); } };
        sum = [&] { product(); while (pos < source.size() && (source[pos] == '+' || source[pos] == '-')) { const auto op=source.mid(pos++,1); product(); appendOp(op); } };
        sum(); if (pos != source.size()) throw std::runtime_error("Unexpected formula suffix");
    }
    double value(double x, double y) const {
        std::array<double,128> stack{}; int n=0;
        for (const auto& op : code) {
            if (op.name=="number") stack[n++]=op.value;
            else if (op.name=="x") stack[n++]=x;
            else if (op.name=="y") stack[n++]=y;
            else if (op.name=="+" || op.name=="-" || op.name=="*" || op.name=="/" || op.name=="^") {
                const double b=stack[--n], a=stack[n-1];
                if(op.name=="+") stack[n-1]=a+b; else if(op.name=="-") stack[n-1]=a-b;
                else if(op.name=="*") stack[n-1]=a*b; else if(op.name=="/") stack[n-1]=a/b;
                else stack[n-1]=std::pow(a,b);
            } else {
                double& a=stack[n-1];
                if(op.name=="neg") a=-a; else if(op.name=="sin") a=std::sin(a); else if(op.name=="cos") a=std::cos(a);
                else if(op.name=="tan") a=std::tan(a); else if(op.name=="sqrt") a=std::sqrt(a);
                else if(op.name=="abs") a=std::abs(a); else if(op.name=="exp") a=std::exp(a); else a=std::log(a);
            }
        }
        return stack[0];
    }
};
}
