#pragma once

#include <string>

namespace prismshift {

class StartupManager {
public:
    static bool IsEnabled();
    static bool SetEnabled(bool enabled);
};

} // namespace prismshift
