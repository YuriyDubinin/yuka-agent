#include "platform/macos_apps.hpp"

namespace yuka::macos {

    using namespace std::chrono_literals;

    std::string applescript_string(const std::string& text) {
        std::string out = "\"";
        for (char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
            }
            out += c;
        }
        return out + "\"";
    }

    CommandResult run_applescript(const std::string& script) {
        CommandResult result = run_command({"/usr/bin/osascript", "-e", script}, 10s);
        while (!result.output.empty() &&
               (result.output.back() == '\n' || result.output.back() == ' ')) {
            result.output.pop_back();
        }
        return result;
    }

    std::optional<std::filesystem::path> bundle_path(const std::string& bundle_id) {
        const CommandResult result = run_applescript(
            "POSIX path of (path to application id " + applescript_string(bundle_id) + ")");
        if (result.exit_code != 0 || result.output.empty()) {
            return std::nullopt;
        }
        return std::filesystem::path(result.output);
    }

    bool is_running(const std::string& bundle_id) {
        return run_applescript("application id " + applescript_string(bundle_id) +
                               " is running")
                   .output == "true";
    }

    bool open_bundle(const std::string& bundle_id) {
        return run_command({"/usr/bin/open", "-b", bundle_id}, 15s).exit_code == 0;
    }

    QuitResult quit(const std::string& bundle_id) {
        if (!is_running(bundle_id)) {
            return QuitResult::not_running;
        }
        // If the app asks about unsaved work, the quit waits for Yura's answer: a timeout
        // here means "asking", not "failed".
        const CommandResult result = run_applescript(
            "with timeout of 5 seconds\n"
            "  tell application id " + applescript_string(bundle_id) + " to quit\n"
            "end timeout");
        if (result.exit_code == 0) {
            return QuitResult::quit;
        }
        if (result.timed_out || result.output.find("-1712") != std::string::npos) {
            return QuitResult::waiting_for_user;
        }
        return QuitResult::failed;
    }

} // namespace yuka::macos
