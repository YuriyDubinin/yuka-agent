#pragma once

#include <string>
#include <vector>

namespace yuka::macos {

    // Windows of another app, through System Events (UI scripting). Works with apps that
    // have no AppleScript dictionary of their own, like VS Code.
    //
    // macOS asks once for permissions for the program Yuka was started from (e.g. Terminal):
    //   Accessibility ("Универсальный доступ") – to read and close windows;
    //   Automation → System Events – to send it commands.

    enum class WindowStatus {
        ok,
        needs_accessibility, // -1719 / -25211: "not allowed assistive access"
        needs_automation,    // -1743: "not authorized to send Apple events"
        failed,
    };

    struct WindowList {
        WindowStatus             status = WindowStatus::failed;
        std::vector<std::string> titles; // empty if the app is not running
    };

    WindowList window_titles(const std::string& bundle_id);

    // Presses the close button of every window with exactly this title (not ⌘W, and without
    // bringing the app forward). `closed` – how many windows were closed.
    WindowStatus close_windows(const std::string& bundle_id, const std::string& title, int& closed);

    // Brings the window with exactly this title to the front.
    WindowStatus raise_window(const std::string& bundle_id, const std::string& title);

    // What to tell Yura when a permission is missing.
    std::string permission_hint(WindowStatus status);

} // namespace yuka::macos
