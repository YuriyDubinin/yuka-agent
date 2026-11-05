#pragma once

#include <string>

namespace yuka::term {

    // ANSI colours shared by all terminal output.
    inline constexpr const char* reset = "\033[0m";

    inline constexpr const char* banner_title = "\033[1;38;5;123m";
    inline constexpr const char* banner_body  = "\033[1;38;5;141m";

    inline constexpr const char* user_prompt = "\033[38;5;81m";
    inline constexpr const char* ai_prompt   = "\033[38;5;141m";
    inline constexpr const char* ai_text     = "\033[38;5;252m";

    inline constexpr const char* info  = "\033[38;5;108m";
    inline constexpr const char* error = "\033[38;5;174m";

    void print_info(const std::string& message);
    void print_error(const std::string& message);

} // namespace yuka::term
