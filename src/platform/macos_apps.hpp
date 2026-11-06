#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "platform/process.hpp"

namespace yuka::macos {

    // A string literal for AppleScript: "…" with \ and " escaped.
    std::string applescript_string(const std::string& text);

    // Runs an AppleScript (osascript -e) with a timeout; output is trimmed.
    CommandResult run_applescript(const std::string& script);

    // Where the app with this bundle id is installed (".../Visual Studio Code.app").
    std::optional<std::filesystem::path> bundle_path(const std::string& bundle_id);

    bool is_running(const std::string& bundle_id);

    // Launches the app, or brings it to the front if it is running (`open -b`).
    bool open_bundle(const std::string& bundle_id);

    enum class QuitResult {
        quit,             // the app quit
        not_running,      // there was nothing to quit
        waiting_for_user, // it is asking about unsaved work
        failed,
    };

    // Quits like ⌘Q: the app itself asks about unsaved work.
    QuitResult quit(const std::string& bundle_id);

} // namespace yuka::macos
