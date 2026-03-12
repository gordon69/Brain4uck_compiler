#pragma once

#include "compiler_types.h"

#include <string>
#include <vector>

namespace bfcc {

std::string EmitAsm(const std::vector<Operation>& ops, Dialect dialect, Arch arch, std::size_t tapeSize);

} // namespace bfcc
