#pragma once

#include "compiler_types.h"

#include <optional>

namespace bfcc {

void PrintUsage();
std::optional<Config> ParseArgs(int argc, char** argv);

} // namespace bfcc
