#include "Core/Common/StringBuilder.h"

#include <cstdio>

namespace schuimm {

std::string formatFixed(double value, int decimals)
{
    char buffer[64];
    const int written = std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    if (written < 0) {
        return {};
    }
    return std::string(buffer, static_cast<std::size_t>(written) < sizeof(buffer) ? static_cast<std::size_t>(written) : sizeof(buffer) - 1);
}

} // namespace schuimm
