#include "brainfuck.h"

#include <iostream>
#include <stack>

namespace bfcc {

std::optional<std::vector<Operation>> ParseBrainfuck(const std::string& source) {
    std::vector<Operation> ops;
    std::stack<std::size_t> loopStack;

    auto pushOrMerge = [&](OpCode code, int delta = 0) {
        if (!ops.empty() && ops.back().code == code &&
            (code == OpCode::MovePtr || code == OpCode::AddValue)) {
            ops.back().argument += delta;
            if (ops.back().argument == 0) {
                ops.pop_back();
            }
            return;
        }
        Operation op{};
        op.code = code;
        op.argument = delta;
        ops.push_back(op);
    };

    for (char ch : source) {
        switch (ch) {
        case '>':
            pushOrMerge(OpCode::MovePtr, +1);
            break;
        case '<':
            pushOrMerge(OpCode::MovePtr, -1);
            break;
        case '+':
            pushOrMerge(OpCode::AddValue, +1);
            break;
        case '-':
            pushOrMerge(OpCode::AddValue, -1);
            break;
        case '.':
            pushOrMerge(OpCode::Output);
            break;
        case ',':
            pushOrMerge(OpCode::Input);
            break;
        case '[':
            loopStack.push(ops.size());
            pushOrMerge(OpCode::LoopStart);
            break;
        case ']':
            if (loopStack.empty()) {
                std::cerr << "Error: unmatched closing bracket ']'.\n";
                return std::nullopt;
            }
            loopStack.pop();
            pushOrMerge(OpCode::LoopEnd);
            break;
        default:
            break;
        }
    }

    if (!loopStack.empty()) {
        std::cerr << "Error: unmatched opening bracket '['.\n";
        return std::nullopt;
    }
    return ops;
}

} // namespace bfcc
