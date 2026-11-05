#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace yuka {

    // The graphical face to launch.
    struct FaceManifest {
        std::string              name;       // shown in terminal messages and the log file name
        std::string              title;      // how the face introduces itself (greeting, prompt)
        std::filesystem::path    executable; // absolute
        std::vector<std::string> args;
    };

    // Directory next to the agent binary that holds the GUI.
    inline constexpr const char* k_gui_dir_name = "gui";
    // The GUI the agent looks for in that directory.
    inline constexpr const char* k_gui_binary_name = "yuka-agent-desktop-ui";
    inline constexpr const char* k_gui_title       = "неплохой электроновский интерфейс";

    // Absolute path to the running binary (empty if it cannot be determined).
    std::filesystem::path executable_path();

    // Absolute path to the directory containing the running binary.
    std::filesystem::path executable_dir();

    // $YUKA_GUI_DIR if set, otherwise <binary dir>/gui.
    std::filesystem::path gui_dir();

    // Looks for yuka-agent-desktop-ui in `dir`:
    //   <dir>/yuka-agent-desktop-ui.app  (macOS bundle – the Electron build)
    //   <dir>/yuka-agent-desktop-ui      (plain executable)
    // Returns std::nullopt if neither exists or is executable.
    std::optional<FaceManifest> find_gui(const std::filesystem::path& dir);

} // namespace yuka
