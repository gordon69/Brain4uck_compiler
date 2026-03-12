#include "internal_pe.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace bfcc {

namespace {

constexpr std::uint32_t kFileAlignment = 0x200;
constexpr std::uint32_t kSectionAlignment = 0x1000;
constexpr std::uint32_t kSectionRva = 0x1000;
constexpr std::uint32_t kHeadersSize = 0x200;

enum class Symbol {
    Tape,
    BytesRead,
    BytesWritten,
    IatGetStdHandle,
    IatReadFile,
    IatWriteFile,
    IatExitProcess
};

struct PatchAbs32 {
    std::size_t off{};
    Symbol symbol{};
};

struct PatchRipRel32 {
    std::size_t dispOff{};
    std::size_t bytesAfterDisp{};
    Symbol symbol{};
};

struct LoopFrame {
    std::size_t start{};
    std::size_t jeDispOff{};
};

struct ImportInfo {
    std::uint32_t importDescOff{};
    std::uint32_t iatOff{};
    std::array<std::uint32_t, 4> iatRvas{};
};

bool WriteBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

constexpr std::uint32_t AlignUp(std::uint32_t v, std::uint32_t a) {
    return (v + a - 1u) / a * a;
}

void WriteU16At(std::vector<std::uint8_t>& buf, std::size_t off, std::uint16_t v) {
    buf[off + 0] = static_cast<std::uint8_t>(v & 0xFFu);
    buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

void WriteU32At(std::vector<std::uint8_t>& buf, std::size_t off, std::uint32_t v) {
    buf[off + 0] = static_cast<std::uint8_t>(v & 0xFFu);
    buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    buf[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    buf[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

void PushU16(std::vector<std::uint8_t>& buf, std::uint16_t v) {
    buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

std::optional<std::uint32_t> ResolveRva(
    Symbol sym,
    std::uint32_t tapeRva,
    std::uint32_t bytesReadRva,
    std::uint32_t bytesWrittenRva,
    const std::array<std::uint32_t, 4>& iatRvas) {
    switch (sym) {
    case Symbol::Tape:
        return tapeRva;
    case Symbol::BytesRead:
        return bytesReadRva;
    case Symbol::BytesWritten:
        return bytesWrittenRva;
    case Symbol::IatGetStdHandle:
        return iatRvas[0];
    case Symbol::IatReadFile:
        return iatRvas[1];
    case Symbol::IatWriteFile:
        return iatRvas[2];
    case Symbol::IatExitProcess:
        return iatRvas[3];
    }
    return std::nullopt;
}

ImportInfo AppendImports(std::vector<std::uint8_t>& sec, bool is64) {
    ImportInfo info{};
    const std::uint32_t thunkSize = is64 ? 8u : 4u;
    info.importDescOff = static_cast<std::uint32_t>(sec.size());
    sec.resize(sec.size() + 40, 0); // 2 IMAGE_IMPORT_DESCRIPTOR
    const std::uint32_t iltOff = static_cast<std::uint32_t>(sec.size());
    sec.resize(sec.size() + 5 * thunkSize, 0);
    info.iatOff = static_cast<std::uint32_t>(sec.size());
    sec.resize(sec.size() + 5 * thunkSize, 0);

    constexpr std::array<const char*, 4> names = {"GetStdHandle", "ReadFile", "WriteFile", "ExitProcess"};
    std::array<std::uint32_t, 4> hintRvas{};
    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::uint32_t off = static_cast<std::uint32_t>(sec.size());
        PushU16(sec, 0);
        for (const char* p = names[i]; *p != '\0'; ++p) {
            sec.push_back(static_cast<std::uint8_t>(*p));
        }
        sec.push_back(0);
        if ((sec.size() & 1u) != 0u) {
            sec.push_back(0);
        }
        hintRvas[i] = kSectionRva + off;
    }

    const std::uint32_t dllNameOff = static_cast<std::uint32_t>(sec.size());
    for (const char c : std::string("KERNEL32.DLL")) {
        sec.push_back(static_cast<std::uint8_t>(c));
    }
    sec.push_back(0);

    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t iltEntryOff = iltOff + i * thunkSize;
        const std::size_t iatEntryOff = info.iatOff + i * thunkSize;
        info.iatRvas[i] = kSectionRva + static_cast<std::uint32_t>(iatEntryOff);
        WriteU32At(sec, iltEntryOff, hintRvas[i]);
        WriteU32At(sec, iatEntryOff, hintRvas[i]);
        if (is64) {
            WriteU32At(sec, iltEntryOff + 4, 0);
            WriteU32At(sec, iatEntryOff + 4, 0);
        }
    }

    WriteU32At(sec, info.importDescOff + 0, kSectionRva + iltOff);      // OriginalFirstThunk
    WriteU32At(sec, info.importDescOff + 12, kSectionRva + dllNameOff); // Name
    WriteU32At(sec, info.importDescOff + 16, kSectionRva + info.iatOff);// FirstThunk
    return info;
}

class Builder64 {
public:
    void emit(std::initializer_list<std::uint8_t> b) { code.insert(code.end(), b.begin(), b.end()); }
    void emitRipPatch(Symbol sym, std::size_t bytesAfterDisp = 0) {
        const std::size_t dispOff = code.size();
        code.resize(code.size() + 4, 0);
        ripPatches.push_back({dispOff, bytesAfterDisp, sym});
    }
    void emitAddPtr(int delta) {
        if (delta == 0) return;
        if (delta > 0) { emit({0x49, 0x81, 0xC4}); } else { emit({0x49, 0x81, 0xEC}); delta = -delta; }
        const std::size_t o = code.size();
        code.resize(code.size() + 4, 0);
        WriteU32At(code, o, static_cast<std::uint32_t>(delta));
    }
    void emitAddCell(int arg) {
        int d = arg % 256;
        if (d < 0) d += 256;
        if (d == 0) return;
        emit({0x41, 0x80, 0x04, 0x24, static_cast<std::uint8_t>(d)});
    }
    void emitLoopStart() {
        const std::size_t start = code.size();
        emit({0x41, 0x80, 0x3C, 0x24, 0x00, 0x0F, 0x84});
        const std::size_t jeOff = code.size();
        code.resize(code.size() + 4, 0);
        loops.push_back({start, jeOff});
    }
    bool emitLoopEnd() {
        if (loops.empty()) return false;
        const LoopFrame lf = loops.back();
        loops.pop_back();
        emit({0x41, 0x80, 0x3C, 0x24, 0x00, 0x0F, 0x85});
        const std::size_t jneOff = code.size();
        code.resize(code.size() + 4, 0);
        const auto back = static_cast<std::int32_t>(static_cast<std::int64_t>(lf.start) - static_cast<std::int64_t>(jneOff + 4));
        WriteU32At(code, jneOff, static_cast<std::uint32_t>(back));
        const auto fwd = static_cast<std::int32_t>(static_cast<std::int64_t>(code.size()) - static_cast<std::int64_t>(lf.jeDispOff + 4));
        WriteU32At(code, lf.jeDispOff, static_cast<std::uint32_t>(fwd));
        return true;
    }

    std::vector<std::uint8_t> code;
    std::vector<PatchRipRel32> ripPatches;
    std::vector<LoopFrame> loops;
};

class Builder86 {
public:
    void emit(std::initializer_list<std::uint8_t> b) { code.insert(code.end(), b.begin(), b.end()); }
    void emitAbsPatch(Symbol sym) {
        const std::size_t off = code.size();
        code.resize(code.size() + 4, 0);
        absPatches.push_back({off, sym});
    }
    void emitAddPtr(int delta) {
        if (delta == 0) return;
        if (delta > 0) { emit({0x81, 0xC6}); } else { emit({0x81, 0xEE}); delta = -delta; }
        const std::size_t o = code.size();
        code.resize(code.size() + 4, 0);
        WriteU32At(code, o, static_cast<std::uint32_t>(delta));
    }
    void emitAddCell(int arg) {
        int d = arg % 256;
        if (d < 0) d += 256;
        if (d == 0) return;
        emit({0x80, 0x06, static_cast<std::uint8_t>(d)}); // add byte [esi],imm8
    }
    void emitLoopStart() {
        const std::size_t start = code.size();
        emit({0x80, 0x3E, 0x00, 0x0F, 0x84});
        const std::size_t jeOff = code.size();
        code.resize(code.size() + 4, 0);
        loops.push_back({start, jeOff});
    }
    bool emitLoopEnd() {
        if (loops.empty()) return false;
        const LoopFrame lf = loops.back();
        loops.pop_back();
        emit({0x80, 0x3E, 0x00, 0x0F, 0x85});
        const std::size_t jneOff = code.size();
        code.resize(code.size() + 4, 0);
        const auto back = static_cast<std::int32_t>(static_cast<std::int64_t>(lf.start) - static_cast<std::int64_t>(jneOff + 4));
        WriteU32At(code, jneOff, static_cast<std::uint32_t>(back));
        const auto fwd = static_cast<std::int32_t>(static_cast<std::int64_t>(code.size()) - static_cast<std::int64_t>(lf.jeDispOff + 4));
        WriteU32At(code, lf.jeDispOff, static_cast<std::uint32_t>(fwd));
        return true;
    }

    std::vector<std::uint8_t> code;
    std::vector<PatchAbs32> absPatches;
    std::vector<LoopFrame> loops;
};

bool BuildX64(const std::vector<Operation>& ops, std::size_t tapeSize, const std::filesystem::path& outPath) {
    constexpr std::uint64_t imageBase = 0x140000000ull;
    Builder64 b;
    b.emit({0x4C, 0x8D, 0x25}); b.emitRipPatch(Symbol::Tape); // lea r12,[rip+..]

    for (const auto& op : ops) {
        switch (op.code) {
        case OpCode::MovePtr: b.emitAddPtr(op.argument); break;
        case OpCode::AddValue: b.emitAddCell(op.argument); break;
        case OpCode::Output:
            b.emit({0xB9, 0xF5, 0xFF, 0xFF, 0xFF});                   // ecx=-11
            b.emit({0x48, 0x83, 0xEC, 0x28});                         // sub rsp,40 (shadow + align)
            b.emit({0xFF, 0x15}); b.emitRipPatch(Symbol::IatGetStdHandle);
            b.emit({0x48, 0x83, 0xC4, 0x28});                         // add rsp,40
            b.emit({0x48, 0x89, 0xC1, 0x49, 0x8D, 0x14, 0x24});       // rcx=rax; rdx=&cell
            b.emit({0x41, 0xB8, 0x01, 0x00, 0x00, 0x00});             // r8d=1
            b.emit({0x4C, 0x8D, 0x0D}); b.emitRipPatch(Symbol::BytesWritten); // r9=&bytesWritten
            b.emit({0x48, 0x83, 0xEC, 0x28, 0x48, 0xC7, 0x44, 0x24, 0x20, 0, 0, 0, 0});
            b.emit({0xFF, 0x15}); b.emitRipPatch(Symbol::IatWriteFile);
            b.emit({0x48, 0x83, 0xC4, 0x28});
            break;
        case OpCode::Input:
            b.emit({0xB9, 0xF6, 0xFF, 0xFF, 0xFF});                   // ecx=-10
            b.emit({0x48, 0x83, 0xEC, 0x28});                         // sub rsp,40 (shadow + align)
            b.emit({0xFF, 0x15}); b.emitRipPatch(Symbol::IatGetStdHandle);
            b.emit({0x48, 0x83, 0xC4, 0x28});                         // add rsp,40
            b.emit({0x48, 0x89, 0xC1, 0x49, 0x8D, 0x14, 0x24});
            b.emit({0x41, 0xB8, 0x01, 0x00, 0x00, 0x00});
            b.emit({0x4C, 0x8D, 0x0D}); b.emitRipPatch(Symbol::BytesRead);
            b.emit({0x48, 0x83, 0xEC, 0x28, 0x48, 0xC7, 0x44, 0x24, 0x20, 0, 0, 0, 0});
            b.emit({0xFF, 0x15}); b.emitRipPatch(Symbol::IatReadFile);
            b.emit({0x48, 0x83, 0xC4, 0x28, 0x83, 0x3D}); b.emitRipPatch(Symbol::BytesRead, 1); b.emit({0x00});
            b.emit({0x0F, 0x85, 0x05, 0x00, 0x00, 0x00, 0x41, 0xC6, 0x04, 0x24, 0x00});
            break;
        case OpCode::LoopStart: b.emitLoopStart(); break;
        case OpCode::LoopEnd:
            if (!b.emitLoopEnd()) return false;
            break;
        }
    }
    if (!b.loops.empty()) return false;
    b.emit({0x31, 0xC9});                                             // xor ecx,ecx
    b.emit({0x48, 0x83, 0xEC, 0x28});                                 // sub rsp,40 (shadow + align)
    b.emit({0xFF, 0x15}); b.emitRipPatch(Symbol::IatExitProcess);
    b.emit({0x48, 0x83, 0xC4, 0x28});                                 // add rsp,40 (fallback)
    b.emit({0xC3});

    const std::uint32_t codeSize = static_cast<std::uint32_t>(b.code.size());
    const std::uint32_t dataOff = AlignUp(codeSize, 16);
    const std::uint32_t tapeOff = dataOff;
    const std::uint32_t bytesReadOff = tapeOff + static_cast<std::uint32_t>(tapeSize);
    const std::uint32_t bytesWrittenOff = bytesReadOff + 8;
    const std::uint32_t importOff = AlignUp(bytesWrittenOff + 8, 8);

    std::vector<std::uint8_t> sec = b.code;
    sec.resize(importOff, 0);
    const ImportInfo imp = AppendImports(sec, true);

    const std::uint32_t tapeRva = kSectionRva + tapeOff;
    const std::uint32_t readRva = kSectionRva + bytesReadOff;
    const std::uint32_t writeRva = kSectionRva + bytesWrittenOff;
    for (const auto& p : b.ripPatches) {
        const auto target = ResolveRva(p.symbol, tapeRva, readRva, writeRva, imp.iatRvas);
        if (!target.has_value()) return false;
        const std::uint32_t nextRva = kSectionRva + static_cast<std::uint32_t>(p.dispOff) + 4u + static_cast<std::uint32_t>(p.bytesAfterDisp);
        const std::int32_t disp = static_cast<std::int32_t>(static_cast<std::int64_t>(*target) - static_cast<std::int64_t>(nextRva));
        WriteU32At(sec, p.dispOff, static_cast<std::uint32_t>(disp));
    }

    const std::uint32_t virtSize = static_cast<std::uint32_t>(sec.size());
    const std::uint32_t rawSize = AlignUp(virtSize, kFileAlignment);
    sec.resize(rawSize, 0);
    const std::uint32_t sizeOfImage = AlignUp(kSectionRva + virtSize, kSectionAlignment);

    std::vector<std::uint8_t> hdr(kHeadersSize, 0);
    hdr[0] = 'M'; hdr[1] = 'Z'; WriteU32At(hdr, 0x3C, 0x80);
    const std::size_t pe = 0x80;
    hdr[pe + 0] = 'P'; hdr[pe + 1] = 'E';
    const std::size_t fh = pe + 4;
    WriteU16At(hdr, fh + 0, 0x8664); WriteU16At(hdr, fh + 2, 1); WriteU16At(hdr, fh + 16, 0xF0); WriteU16At(hdr, fh + 18, 0x0022);
    const std::size_t opt = fh + 20;
    WriteU16At(hdr, opt + 0, 0x20B); hdr[opt + 2] = 0x0E;
    WriteU32At(hdr, opt + 4, codeSize); WriteU32At(hdr, opt + 8, virtSize - codeSize);
    WriteU32At(hdr, opt + 16, kSectionRva); WriteU32At(hdr, opt + 20, kSectionRva);
    for (int i = 0; i < 8; ++i) hdr[opt + 24 + i] = static_cast<std::uint8_t>((imageBase >> (8 * i)) & 0xFFu);
    WriteU32At(hdr, opt + 32, kSectionAlignment); WriteU32At(hdr, opt + 36, kFileAlignment);
    WriteU16At(hdr, opt + 40, 6); WriteU16At(hdr, opt + 48, 6); WriteU32At(hdr, opt + 56, sizeOfImage); WriteU32At(hdr, opt + 60, kHeadersSize);
    WriteU16At(hdr, opt + 68, 3); WriteU32At(hdr, opt + 108, 16);
    const std::size_t dd = opt + 112;
    WriteU32At(hdr, dd + 8, kSectionRva + imp.importDescOff); WriteU32At(hdr, dd + 12, 40);
    WriteU32At(hdr, dd + 12 * 8, kSectionRva + imp.iatOff); WriteU32At(hdr, dd + 12 * 8 + 4, 40);
    const std::size_t sh = opt + 0xF0;
    hdr[sh + 0] = '.'; hdr[sh + 1] = 't'; hdr[sh + 2] = 'e'; hdr[sh + 3] = 'x'; hdr[sh + 4] = 't';
    WriteU32At(hdr, sh + 8, virtSize); WriteU32At(hdr, sh + 12, kSectionRva); WriteU32At(hdr, sh + 16, rawSize); WriteU32At(hdr, sh + 20, kHeadersSize);
    WriteU32At(hdr, sh + 36, 0xE0000060);

    std::vector<std::uint8_t> img;
    img.insert(img.end(), hdr.begin(), hdr.end());
    img.insert(img.end(), sec.begin(), sec.end());
    return WriteBinaryFile(outPath, img);
}

bool BuildX86(const std::vector<Operation>& ops, std::size_t tapeSize, const std::filesystem::path& outPath) {
    constexpr std::uint32_t imageBase = 0x00400000u;
    Builder86 b;
    b.emit({0xBE}); b.emitAbsPatch(Symbol::Tape); // mov esi, imm32

    for (const auto& op : ops) {
        switch (op.code) {
        case OpCode::MovePtr: b.emitAddPtr(op.argument); break;
        case OpCode::AddValue: b.emitAddCell(op.argument); break;
        case OpCode::Output:
            b.emit({0x6A, 0xF5, 0xFF, 0x15}); b.emitAbsPatch(Symbol::IatGetStdHandle); // push -11; call [GetStdHandle]
            b.emit({0x6A, 0x00});                         // lpOverlapped
            b.emit({0x68}); b.emitAbsPatch(Symbol::BytesWritten); // lpWritten
            b.emit({0x6A, 0x01});                         // size
            b.emit({0x56});                               // buffer (esi)
            b.emit({0x50});                               // handle (eax)
            b.emit({0xFF, 0x15}); b.emitAbsPatch(Symbol::IatWriteFile);
            break;
        case OpCode::Input:
            b.emit({0x6A, 0xF6, 0xFF, 0x15}); b.emitAbsPatch(Symbol::IatGetStdHandle); // push -10; call [GetStdHandle]
            b.emit({0x6A, 0x00});
            b.emit({0x68}); b.emitAbsPatch(Symbol::BytesRead);
            b.emit({0x6A, 0x01});
            b.emit({0x56});
            b.emit({0x50});
            b.emit({0xFF, 0x15}); b.emitAbsPatch(Symbol::IatReadFile);
            b.emit({0x83, 0x3D}); b.emitAbsPatch(Symbol::BytesRead); b.emit({0x00}); // cmp dword [bytesRead],0
            b.emit({0x0F, 0x85, 0x03, 0x00, 0x00, 0x00, 0xC6, 0x06, 0x00}); // jne +3; mov byte [esi],0
            break;
        case OpCode::LoopStart: b.emitLoopStart(); break;
        case OpCode::LoopEnd:
            if (!b.emitLoopEnd()) return false;
            break;
        }
    }
    if (!b.loops.empty()) return false;
    b.emit({0x6A, 0x00, 0xFF, 0x15}); b.emitAbsPatch(Symbol::IatExitProcess); b.emit({0xC3}); // push 0; call [ExitProcess]

    const std::uint32_t codeSize = static_cast<std::uint32_t>(b.code.size());
    const std::uint32_t dataOff = AlignUp(codeSize, 16);
    const std::uint32_t tapeOff = dataOff;
    const std::uint32_t bytesReadOff = tapeOff + static_cast<std::uint32_t>(tapeSize);
    const std::uint32_t bytesWrittenOff = bytesReadOff + 4;
    const std::uint32_t importOff = AlignUp(bytesWrittenOff + 4, 4);

    std::vector<std::uint8_t> sec = b.code;
    sec.resize(importOff, 0);
    const ImportInfo imp = AppendImports(sec, false);

    const std::uint32_t tapeRva = kSectionRva + tapeOff;
    const std::uint32_t readRva = kSectionRva + bytesReadOff;
    const std::uint32_t writeRva = kSectionRva + bytesWrittenOff;
    for (const auto& p : b.absPatches) {
        const auto rva = ResolveRva(p.symbol, tapeRva, readRva, writeRva, imp.iatRvas);
        if (!rva.has_value()) return false;
        WriteU32At(sec, p.off, imageBase + *rva);
    }

    const std::uint32_t virtSize = static_cast<std::uint32_t>(sec.size());
    const std::uint32_t rawSize = AlignUp(virtSize, kFileAlignment);
    sec.resize(rawSize, 0);
    const std::uint32_t sizeOfImage = AlignUp(kSectionRva + virtSize, kSectionAlignment);

    std::vector<std::uint8_t> hdr(kHeadersSize, 0);
    hdr[0] = 'M'; hdr[1] = 'Z'; WriteU32At(hdr, 0x3C, 0x80);
    const std::size_t pe = 0x80;
    hdr[pe + 0] = 'P'; hdr[pe + 1] = 'E';
    const std::size_t fh = pe + 4;
    WriteU16At(hdr, fh + 0, 0x14C); WriteU16At(hdr, fh + 2, 1); WriteU16At(hdr, fh + 16, 0xE0); WriteU16At(hdr, fh + 18, 0x0102);
    const std::size_t opt = fh + 20;
    WriteU16At(hdr, opt + 0, 0x10B); hdr[opt + 2] = 0x0E;
    WriteU32At(hdr, opt + 4, codeSize); WriteU32At(hdr, opt + 8, virtSize - codeSize);
    WriteU32At(hdr, opt + 16, kSectionRva); WriteU32At(hdr, opt + 20, kSectionRva); WriteU32At(hdr, opt + 24, kSectionRva);
    WriteU32At(hdr, opt + 28, imageBase); WriteU32At(hdr, opt + 32, kSectionAlignment); WriteU32At(hdr, opt + 36, kFileAlignment);
    WriteU16At(hdr, opt + 40, 6); WriteU16At(hdr, opt + 48, 6); WriteU32At(hdr, opt + 56, sizeOfImage); WriteU32At(hdr, opt + 60, kHeadersSize);
    WriteU16At(hdr, opt + 68, 3);
    WriteU32At(hdr, opt + 72, 1u << 20); WriteU32At(hdr, opt + 76, 1u << 12); WriteU32At(hdr, opt + 80, 1u << 20); WriteU32At(hdr, opt + 84, 1u << 12);
    WriteU32At(hdr, opt + 92, 16);
    const std::size_t dd = opt + 96;
    WriteU32At(hdr, dd + 8, kSectionRva + imp.importDescOff); WriteU32At(hdr, dd + 12, 40);
    WriteU32At(hdr, dd + 12 * 8, kSectionRva + imp.iatOff); WriteU32At(hdr, dd + 12 * 8 + 4, 20);
    const std::size_t sh = opt + 0xE0;
    hdr[sh + 0] = '.'; hdr[sh + 1] = 't'; hdr[sh + 2] = 'e'; hdr[sh + 3] = 'x'; hdr[sh + 4] = 't';
    WriteU32At(hdr, sh + 8, virtSize); WriteU32At(hdr, sh + 12, kSectionRva); WriteU32At(hdr, sh + 16, rawSize); WriteU32At(hdr, sh + 20, kHeadersSize);
    WriteU32At(hdr, sh + 36, 0xE0000060);

    std::vector<std::uint8_t> img;
    img.insert(img.end(), hdr.begin(), hdr.end());
    img.insert(img.end(), sec.begin(), sec.end());
    return WriteBinaryFile(outPath, img);
}

} // namespace

bool BuildInternalExe(const std::vector<Operation>& ops, Arch arch, std::size_t tapeSize, const std::filesystem::path& outputPath) {
    if (arch == Arch::X64) {
        return BuildX64(ops, tapeSize, outputPath);
    }
    return BuildX86(ops, tapeSize, outputPath);
}

} // namespace bfcc
