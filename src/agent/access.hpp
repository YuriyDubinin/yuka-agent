#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace yuka {

    // What Yuka may touch on this computer and how, technically – config/access.json.
    // Yura writes it by hand; the agent only reads it. Anything not listed does not exist
    // for the model.
    //
    //   {
    //     "apps": [
    //       {
    //         "id": "vscode", "name": "Visual Studio Code", "description": "редактор кода",
    //         "bundle_id": "com.microsoft.VSCode", "open": true, "close": true,
    //         "open_folder": { "cli": "Contents/Resources/app/bin/code",
    //                          "args": ["--new-window", "{path}"] },
    //         "window_title": "{folder}"
    //       }
    //     ],
    //     "folders": [
    //       { "id": "projects", "name": "Проекты", "path": "~/Documents/Projects",
    //         "contains": "projects", "open_with": "vscode" }
    //     ]
    //   }

    // How an app opens a folder: a command-line tool inside its bundle.
    struct OpenFolderCommand {
        std::string              cli;  // relative to the .app bundle, no ".."
        std::vector<std::string> args; // "{path}" is replaced by the folder
    };

    struct AccessApp {
        std::string                      id;
        std::string                      name;
        std::string                      description;
        std::string                      bundle_id; // e.g. com.microsoft.VSCode
        bool                             can_open  = false;
        bool                             can_close = false;
        std::optional<OpenFolderCommand> open_folder;
        // How a window of an opened folder is recognised: one " — "-separated part of the
        // window title equals this, with {folder} replaced by the folder name.
        std::string window_title = "{folder}";
    };

    struct AccessFolder {
        std::string           id;
        std::string           name;
        std::filesystem::path path;      // absolute, symlinks resolved
        std::string           contains;  // "projects": every subfolder is a project
        std::string           open_with; // id of an app with open_folder
    };

    class AccessConfig {
    public:
        inline static const char* const k_file_name        = "access.json";
        inline static const char* const k_legacy_file_name = "apps.json";

        // Loads <config_dir>/access.json (or the older apps.json). A missing file means no
        // access; broken entries are skipped with a warning – the agent starts anyway.
        static AccessConfig load(const std::filesystem::path& config_dir);

        bool empty() const { return apps_.empty() && folders_.empty(); }

        const std::vector<AccessApp>&    apps() const { return apps_; }
        const std::vector<AccessFolder>& folders() const { return folders_; }
        const AccessApp*                 find_app(const std::string& id) const;

    private:
        std::vector<AccessApp>    apps_;
        std::vector<AccessFolder> folders_;
    };

    // True if `path` (resolved) lies inside `root` (resolved) – never above it.
    bool is_inside(const std::filesystem::path& path, const std::filesystem::path& root);

} // namespace yuka
