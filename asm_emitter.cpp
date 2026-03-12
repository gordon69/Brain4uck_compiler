#include "asm_emitter.h"

#include <iostream>
#include <sstream>
#include <vector>

namespace bfcc {

namespace {

std::string EmitAsmNasm(const std::vector<Operation>& ops, Arch arch, std::size_t tapeSize) {
    std::ostringstream out;

    if (arch == Arch::X64) {
        out << "default rel\n"
            << "extern putchar\n"
            << "extern getchar\n"
            << "global main\n\n"
            << "section .bss\n"
            << "tape: resb " << tapeSize << "\n\n"
            << "section .text\n"
            << "main:\n"
            << "    push rbx\n"
            << "    sub rsp, 32\n"
            << "    lea rbx, [tape]\n";
    } else {
        out << "extern _putchar\n"
            << "extern _getchar\n"
            << "global _main\n\n"
            << "section .bss\n"
            << "tape: resb " << tapeSize << "\n\n"
            << "section .text\n"
            << "_main:\n"
            << "    push ebx\n"
            << "    mov ebx, tape\n";
    }

    std::vector<int> loopIds;
    int nextLoopId = 0;

    for (const Operation& op : ops) {
        switch (op.code) {
        case OpCode::MovePtr:
            if (op.argument != 0) {
                if (arch == Arch::X64) {
                    out << "    add rbx, " << op.argument << "\n";
                } else {
                    out << "    add ebx, " << op.argument << "\n";
                }
            }
            break;
        case OpCode::AddValue:
            if (op.argument > 0) {
                out << "    add byte ["
                    << (arch == Arch::X64 ? "rbx" : "ebx")
                    << "], " << op.argument << "\n";
            } else if (op.argument < 0) {
                out << "    sub byte ["
                    << (arch == Arch::X64 ? "rbx" : "ebx")
                    << "], " << (-op.argument) << "\n";
            }
            break;
        case OpCode::Output:
            if (arch == Arch::X64) {
                out << "    movzx ecx, byte [rbx]\n"
                    << "    call putchar\n";
            } else {
                out << "    movzx eax, byte [ebx]\n"
                    << "    push eax\n"
                    << "    call _putchar\n"
                    << "    add esp, 4\n";
            }
            break;
        case OpCode::Input:
            if (arch == Arch::X64) {
                out << "    call getchar\n"
                    << "    cmp eax, -1\n"
                    << "    je .input_eof_" << nextLoopId << "\n"
                    << "    mov [rbx], al\n"
                    << "    jmp .input_done_" << nextLoopId << "\n"
                    << ".input_eof_" << nextLoopId << ":\n"
                    << "    mov byte [rbx], 0\n"
                    << ".input_done_" << nextLoopId << ":\n";
            } else {
                out << "    call _getchar\n"
                    << "    cmp eax, -1\n"
                    << "    je .input_eof_" << nextLoopId << "\n"
                    << "    mov [ebx], al\n"
                    << "    jmp .input_done_" << nextLoopId << "\n"
                    << ".input_eof_" << nextLoopId << ":\n"
                    << "    mov byte [ebx], 0\n"
                    << ".input_done_" << nextLoopId << ":\n";
            }
            ++nextLoopId;
            break;
        case OpCode::LoopStart: {
            const int id = nextLoopId++;
            loopIds.push_back(id);
            out << ".loop_start_" << id << ":\n"
                << "    cmp byte [" << (arch == Arch::X64 ? "rbx" : "ebx") << "], 0\n"
                << "    je .loop_end_" << id << "\n";
            break;
        }
        case OpCode::LoopEnd: {
            if (loopIds.empty()) {
                std::cerr << "Internal error: loop stack underflow.\n";
                return {};
            }
            const int id = loopIds.back();
            loopIds.pop_back();
            out << "    cmp byte [" << (arch == Arch::X64 ? "rbx" : "ebx") << "], 0\n"
                << "    jne .loop_start_" << id << "\n"
                << ".loop_end_" << id << ":\n";
            break;
        }
        }
    }

    if (arch == Arch::X64) {
        out << "    xor eax, eax\n"
            << "    add rsp, 32\n"
            << "    pop rbx\n"
            << "    ret\n";
    } else {
        out << "    xor eax, eax\n"
            << "    pop ebx\n"
            << "    ret\n";
    }
    return out.str();
}

std::string EmitAsmFasm(const std::vector<Operation>& ops, Arch arch, std::size_t tapeSize) {
    std::string body = EmitAsmNasm(ops, arch, tapeSize);
    std::ostringstream out;
    if (arch == Arch::X64) {
        out << "format MS64 COFF\n";
    } else {
        out << "format MS COFF\n";
    }
    out << body;
    return out.str();
}

std::string EmitAsmMasm(const std::vector<Operation>& ops, Arch arch, std::size_t tapeSize) {
    std::ostringstream out;
    out << ".model flat, c\n"
        << "option casemap:none\n\n"
        << "EXTERN getchar:PROC\n"
        << "EXTERN putchar:PROC\n"
        << "PUBLIC main\n\n"
        << ".data?\n"
        << "tape db " << tapeSize << " dup(?)\n\n"
        << ".code\n"
        << "main PROC\n";

    if (arch == Arch::X64) {
        out << "    push rbx\n"
            << "    sub rsp, 32\n"
            << "    lea rbx, tape\n";
    } else {
        out << "    push ebx\n"
            << "    mov ebx, OFFSET tape\n";
    }

    std::vector<int> loopIds;
    int nextLoopId = 0;
    for (const Operation& op : ops) {
        switch (op.code) {
        case OpCode::MovePtr:
            if (op.argument != 0) {
                out << "    add " << (arch == Arch::X64 ? "rbx" : "ebx") << ", " << op.argument << "\n";
            }
            break;
        case OpCode::AddValue:
            if (op.argument > 0) {
                out << "    add byte ptr [" << (arch == Arch::X64 ? "rbx" : "ebx") << "], " << op.argument << "\n";
            } else if (op.argument < 0) {
                out << "    sub byte ptr [" << (arch == Arch::X64 ? "rbx" : "ebx") << "], " << (-op.argument) << "\n";
            }
            break;
        case OpCode::Output:
            if (arch == Arch::X64) {
                out << "    movzx ecx, byte ptr [rbx]\n"
                    << "    call putchar\n";
            } else {
                out << "    movzx eax, byte ptr [ebx]\n"
                    << "    push eax\n"
                    << "    call putchar\n"
                    << "    add esp, 4\n";
            }
            break;
        case OpCode::Input:
            out << "    call getchar\n"
                << "    cmp eax, -1\n"
                << "    je input_eof_" << nextLoopId << "\n"
                << "    mov byte ptr [" << (arch == Arch::X64 ? "rbx" : "ebx") << "], al\n"
                << "    jmp input_done_" << nextLoopId << "\n"
                << "input_eof_" << nextLoopId << ":\n"
                << "    mov byte ptr [" << (arch == Arch::X64 ? "rbx" : "ebx") << "], 0\n"
                << "input_done_" << nextLoopId << ":\n";
            ++nextLoopId;
            break;
        case OpCode::LoopStart: {
            const int id = nextLoopId++;
            loopIds.push_back(id);
            out << "loop_start_" << id << ":\n"
                << "    cmp byte ptr [" << (arch == Arch::X64 ? "rbx" : "ebx") << "], 0\n"
                << "    je loop_end_" << id << "\n";
            break;
        }
        case OpCode::LoopEnd: {
            if (loopIds.empty()) {
                return {};
            }
            const int id = loopIds.back();
            loopIds.pop_back();
            out << "    cmp byte ptr [" << (arch == Arch::X64 ? "rbx" : "ebx") << "], 0\n"
                << "    jne loop_start_" << id << "\n"
                << "loop_end_" << id << ":\n";
            break;
        }
        }
    }

    if (arch == Arch::X64) {
        out << "    xor eax, eax\n"
            << "    add rsp, 32\n"
            << "    pop rbx\n"
            << "    ret\n";
    } else {
        out << "    xor eax, eax\n"
            << "    pop ebx\n"
            << "    ret\n";
    }
    out << "main ENDP\nEND\n";
    return out.str();
}

} // namespace

std::string EmitAsm(const std::vector<Operation>& ops, Dialect dialect, Arch arch, std::size_t tapeSize) {
    switch (dialect) {
    case Dialect::Nasm:
        return EmitAsmNasm(ops, arch, tapeSize);
    case Dialect::Fasm:
        return EmitAsmFasm(ops, arch, tapeSize);
    case Dialect::Masm:
        return EmitAsmMasm(ops, arch, tapeSize);
    }
    return {};
}

} // namespace bfcc
