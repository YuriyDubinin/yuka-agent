#pragma once

#include <string>

namespace yuka {

    // Escapes a string for safe use inside single quotes in a POSIX shell command.
    inline std::string shell_escape_single_quotes(const std::string& input) {
        std::string escaped;
        escaped.reserve(input.size() + 16);

        for (char c : input) {
            if (c == '\'') {
                escaped += "'\\''";
            } else {
                escaped += c;
            }
        }

        return escaped;
    }

} // namespace yuka
