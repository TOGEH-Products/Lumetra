#pragma once

#include "profiles/ColorProfile.h"
#include <vector>

namespace prismshift {

class BuiltinGames {
public:
    static std::vector<ColorProfile> GetDefaultList();
};

} // namespace prismshift
