#include "asm_emitter.h"
#include "brainfuck.h"
#include "cli.h"
#include "internal_pe.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace bfcc {

std::optional<std::string> ReadTextFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool WriteTextFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << content;
    return static_cast<bool>(out);
}

} // namespace bfcc

int main(int argc, char** argv) {
    const auto cfgOpt = bfcc::ParseArgs(argc, argv);
    if (!cfgOpt.has_value()) {
        return (argc >= 2 && std::string(argv[1]) == "--help") ? 0 : 1;
    }
    const bfcc::Config cfg = *cfgOpt;

    const auto sourceOpt = bfcc::ReadTextFile(cfg.inputFile);
    if (!sourceOpt.has_value()) {
        std::cerr << "Error: cannot read input file '" << cfg.inputFile.string() << "'.\n";
        return 1;
    }

    const auto opsOpt = bfcc::ParseBrainfuck(*sourceOpt);
    if (!opsOpt.has_value()) {
        return 1;
    }

    if (cfg.emitKind == bfcc::EmitKind::Asm) {
        const std::string asmCode = bfcc::EmitAsm(*opsOpt, cfg.dialect, cfg.arch, cfg.tapeSize);
        if (asmCode.empty()) {
            std::cerr << "Error: failed to generate assembly.\n";
            return 1;
        }
        if (!bfcc::WriteTextFile(cfg.outputFile, asmCode)) {
            std::cerr << "Error: cannot write asm file '" << cfg.outputFile.string() << "'.\n";
            return 1;
        }
        std::cout << "ASM generated: " << cfg.outputFile.string() << "\n";
        return 0;
    }

    if (!bfcc::BuildInternalExe(*opsOpt, cfg.arch, cfg.tapeSize, cfg.outputFile)) {
        std::cerr << "Error: failed to build internal PE executable.\n";
        return 1;
    }
    std::cout << "Executable generated (internal): " << cfg.outputFile.string() << "\n";
    return 0;
}
