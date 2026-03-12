#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace bfcc {

namespace fs = std::filesystem;

enum class EmitKind {
    Asm,
    Exe
};

enum class Arch {
    X86,
    X64
};

enum class Dialect {
    Nasm,
    Fasm,
    Masm
};

enum class OpCode {
    MovePtr,
    AddValue,
    Input,
    Output,
    LoopStart,
    LoopEnd
};

struct Operation {
    OpCode code{};
    int argument{};
};

struct Config {
    fs::path inputFile;
    fs::path outputFile;
    EmitKind emitKind{EmitKind::Asm};
    Arch arch{Arch::X64};
    Dialect dialect{Dialect::Nasm};
    std::size_t tapeSize{30000};
};

} // namespace bfcc
