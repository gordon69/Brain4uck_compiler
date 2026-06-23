#include "cli.h"

#include <cctype>
#include <iostream>
#include <string>
#include <string_view>

namespace bfcc {

namespace {

std::string ToLower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::optional<EmitKind> ParseEmitKind(const std::string& value) {
    const std::string lowered = ToLower(value);
    if (lowered == "asm") {
        return EmitKind::Asm;
    }
    if (lowered == "exe") {
        return EmitKind::Exe;
    }
    return std::nullopt;
}

std::optional<Arch> ParseArch(const std::string& value) {
    const std::string lowered = ToLower(value);
    if (lowered == "x86" || lowered == "win32" || lowered == "32") {
        return Arch::X86;
    }
    if (lowered == "x64" || lowered == "amd64" || lowered == "64") {
        return Arch::X64;
    }
    return std::nullopt;
}

std::optional<Dialect> ParseDialect(const std::string& value) {
    const std::string lowered = ToLower(value);
    if (lowered == "nasm") {
        return Dialect::Nasm;
    }
    if (lowered == "fasm") {
        return Dialect::Fasm;
    }
    if (lowered == "masm") {
        return Dialect::Masm;
    }
    return std::nullopt;
}

fs::path DefaultOutputPath(const Config& cfg) {
    fs::path output = cfg.inputFile;
    if (cfg.emitKind == EmitKind::Asm) {
        output.replace_extension(".asm");
    } else {
        output.replace_extension(".exe");
    }
    return output;
}

} // namespace

void PrintUsage() {
    std::cout
        << "Brainfuck compiler\n"
        << "Usage:\n"
        << "  bfcc <input.bf> [options]\n\n"
        << "Options:\n"
        << "  --emit <asm|exe>          Output artifact kind (default: asm)\n"
        << "  --arch <x86|x64>          Target architecture (default: x64)\n"
        << "  --dialect <nasm|fasm|masm> ASM dialect for --emit asm (default: nasm)\n"
        << "  -o, --output <path>       Output file path\n"
        << "  --tape-size <N>           Tape size in bytes (default: 30000)\n"
        << "  -h, --help                Show help\n";
}

std::optional<Config> ParseArgs(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return std::nullopt;
    }

    Config cfg{};
    bool inputSet = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            PrintUsage();
            return std::nullopt;
        }

        if (!arg.empty() && arg[0] != '-') {
            if (inputSet) {
                std::cerr << "Error: multiple input files are not supported.\n";
                return std::nullopt;
            }
            cfg.inputFile = fs::path(arg);
            inputSet = true;
            continue;
        }

        auto requireValue = [&](std::string_view option) -> std::optional<std::string> {
            if (i + 1 >= argc) {
                std::cerr << "Error: missing value for " << option << ".\n";
                return std::nullopt;
            }
            ++i;
            return std::string(argv[i]);
        };

        if (arg == "--emit") {
            auto value = requireValue(arg);
            if (!value.has_value()) {
                return std::nullopt;
            }
            auto parsed = ParseEmitKind(*value);
            if (!parsed.has_value()) {
                std::cerr << "Error: invalid --emit value '" << *value << "'.\n";
                return std::nullopt;
            }
            cfg.emitKind = *parsed;
        } else if (arg == "--arch") {
            auto value = requireValue(arg);
            if (!value.has_value()) {
                return std::nullopt;
            }
            auto parsed = ParseArch(*value);
            if (!parsed.has_value()) {
                std::cerr << "Error: invalid --arch value '" << *value << "'.\n";
                return std::nullopt;
            }
            cfg.arch = *parsed;
        } else if (arg == "--dialect") {
            auto value = requireValue(arg);
            if (!value.has_value()) {
                return std::nullopt;
            }
            auto parsed = ParseDialect(*value);
            if (!parsed.has_value()) {
                std::cerr << "Error: invalid --dialect value '" << *value << "'.\n";
                return std::nullopt;
            }
            cfg.dialect = *parsed;
        } else if (arg == "-o" || arg == "--output") {
            auto value = requireValue(arg);
            if (!value.has_value()) {
                return std::nullopt;
            }
            cfg.outputFile = fs::path(*value);
        } else if (arg == "--tape-size") {
            auto value = requireValue(arg);
            if (!value.has_value()) {
                return std::nullopt;
            }
            try {
                const std::size_t parsed = static_cast<std::size_t>(std::stoull(*value));
                if (parsed == 0) {
                    std::cerr << "Error: --tape-size must be > 0.\n";
                    return std::nullopt;
                }
                cfg.tapeSize = parsed;
            } catch (...) {
                std::cerr << "Error: invalid --tape-size value '" << *value << "'.\n";
                return std::nullopt;
            }
        } else {
            std::cerr << "Error: unknown option '" << arg << "'.\n";
            return std::nullopt;
        }
    }

    if (!inputSet) {
        std::cerr << "Error: input file is required.\n";
        PrintUsage();
        return std::nullopt;
    }
    if (cfg.outputFile.empty()) {
        cfg.outputFile = DefaultOutputPath(cfg);
    }
    return cfg;
}

} // namespace bfcc
