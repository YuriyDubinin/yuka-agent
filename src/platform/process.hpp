#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace yuka {

    struct CommandResult {
        int         exit_code = -1; // -1: could not start or was killed after the timeout
        bool        timed_out = false;
        std::string output;         // stdout and stderr, merged
    };

    // Runs a program directly (posix_spawn, no shell: arguments are passed as they are, so
    // nothing in them is interpreted). argv[0] must be an absolute path. The program is
    // killed if it runs longer than `timeout`.
    CommandResult run_command(const std::vector<std::string>& argv,
                              std::chrono::milliseconds       timeout);

} // namespace yuka
