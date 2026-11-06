#include "platform/macos_windows.hpp"

#include <sstream>

#include "platform/macos_apps.hpp"

namespace yuka::macos {

    namespace {

        WindowStatus status_of(const CommandResult& result) {
            if (result.exit_code == 0) {
                return WindowStatus::ok;
            }
            const std::string& out = result.output;
            if (out.find("-1719") != std::string::npos || out.find("-25211") != std::string::npos) {
                return WindowStatus::needs_accessibility;
            }
            if (out.find("-1743") != std::string::npos) {
                return WindowStatus::needs_automation;
            }
            return WindowStatus::failed;
        }

        // "every process whose bundle identifier is …": none if the app is not running.
        std::string processes_of(const std::string& bundle_id) {
            return "(every process whose bundle identifier is " + applescript_string(bundle_id) +
                   ")";
        }

    } // namespace

    WindowList window_titles(const std::string& bundle_id) {
        // One title per line: names may contain commas, so the default list text is no good.
        const CommandResult result = run_applescript(
            "tell application \"System Events\"\n"
            "  set out to \"\"\n"
            "  repeat with p in " + processes_of(bundle_id) + "\n"
            "    repeat with w in windows of p\n"
            "      set out to out & (name of w) & linefeed\n"
            "    end repeat\n"
            "  end repeat\n"
            "  return out\n"
            "end tell");

        WindowList list;
        list.status = status_of(result);
        if (list.status != WindowStatus::ok) {
            return list;
        }
        std::istringstream lines(result.output);
        std::string        line;
        while (std::getline(lines, line)) {
            if (!line.empty()) {
                list.titles.push_back(line);
            }
        }
        return list;
    }

    WindowStatus close_windows(const std::string& bundle_id, const std::string& title, int& closed) {
        closed                     = 0;
        const CommandResult result = run_applescript(
            "tell application \"System Events\"\n"
            "  set n to 0\n"
            "  repeat with p in " + processes_of(bundle_id) + "\n"
            "    repeat with w in (windows of p whose name is " + applescript_string(title) + ")\n"
            "      click (first button of w whose subrole is \"AXCloseButton\")\n"
            "      set n to n + 1\n"
            "    end repeat\n"
            "  end repeat\n"
            "  return n\n"
            "end tell");
        const WindowStatus status = status_of(result);
        if (status == WindowStatus::ok) {
            try {
                closed = std::stoi(result.output);
            } catch (...) {
                closed = 0;
            }
        }
        return status;
    }

    WindowStatus raise_window(const std::string& bundle_id, const std::string& title) {
        const CommandResult result = run_applescript(
            "tell application \"System Events\"\n"
            "  repeat with p in " + processes_of(bundle_id) + "\n"
            "    repeat with w in (windows of p whose name is " + applescript_string(title) + ")\n"
            "      perform action \"AXRaise\" of w\n"
            "      set frontmost of p to true\n"
            "      return \"raised\"\n"
            "    end repeat\n"
            "  end repeat\n"
            "  return \"none\"\n"
            "end tell");
        const WindowStatus status = status_of(result);
        if (status == WindowStatus::ok && result.output != "raised") {
            return WindowStatus::failed;
        }
        return status;
    }

    std::string permission_hint(WindowStatus status) {
        switch (status) {
            case WindowStatus::needs_accessibility:
                return "macOS не разрешает управлять окнами. Открой Системные настройки → "
                       "Конфиденциальность и безопасность → Универсальный доступ и включи "
                       "программу, из которой запущена Юка (например, Терминал), затем "
                       "перезапусти Юку.";
            case WindowStatus::needs_automation:
                return "macOS не разрешает управлять окнами через System Events. Открой "
                       "Системные настройки → Конфиденциальность и безопасность → Автоматизация "
                       "и разреши программе, из которой запущена Юка, управлять «System Events».";
            case WindowStatus::ok:
            case WindowStatus::failed:
                break;
        }
        return "Не удалось получить список окон.";
    }

} // namespace yuka::macos
