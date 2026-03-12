#pragma once

#include "compiler_types.h"

#include <optional>
#include <string>
#include <vector>

namespace bfcc {

std::optional<std::vector<Operation>> ParseBrainfuck(const std::string& source);

} // namespace bfcc
