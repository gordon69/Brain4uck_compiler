#pragma once

#include "compiler_types.h"

#include <filesystem>
#include <vector>

namespace bfcc {

bool BuildInternalExe(const std::vector<Operation>& ops, Arch arch, std::size_t tapeSize, const std::filesystem::path& outputPath);

} // namespace bfcc
