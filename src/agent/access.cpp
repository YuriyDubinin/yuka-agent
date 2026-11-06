#include "agent/access.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

#include "platform/terminal.hpp"

namespace yuka {

    namespace {

        namespace fs = std::filesystem;
        using nlohmann::json;

        bool is_valid_id(const std::string& id) {
            return !id.empty() && id.size() <= 64 &&
                   std::all_of(id.begin(), id.end(), [](char c) {
                       return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                              c == '_';
                   });
        }

        // Reverse-DNS only: bundle ids end up inside AppleScript strings.
        bool is_valid_bundle_id(const std::string& id) {
            return !id.empty() && id.size() <= 255 && id.find('.') != std::string::npos &&
                   std::all_of(id.begin(), id.end(), [](char c) {
                       return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
                   });
        }

        std::string string_field(const json& object, const char* key) {
            const auto it = object.find(key);
            return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
        }

        bool bool_field(const json& object, const char* key) {
            const auto it = object.find(key);
            return it != object.end() && it->is_boolean() && it->get<bool>();
        }

        fs::path expand_home(const std::string& path) {
            if (path == "~" || path.rfind("~/", 0) == 0) {
                const char* home = std::getenv("HOME");
                return fs::path(home != nullptr ? home : "") / path.substr(path.size() > 1 ? 2 : 1);
            }
            return fs::path(path);
        }

        class Loader {
        public:
            explicit Loader(fs::path file) : file_(std::move(file)) {}

            void warn(const std::string& what, const std::string& why) const {
                term::print_error(what + " в " + file_.string() + " пропущена: " + why + ".");
            }

            std::optional<AccessApp> app(const json& entry, std::size_t index) const {
                AccessApp app;
                if (entry.is_object()) {
                    app.id          = string_field(entry, "id");
                    app.name        = string_field(entry, "name");
                    app.description = string_field(entry, "description");
                    app.bundle_id   = string_field(entry, "bundle_id");
                    app.can_open    = bool_field(entry, "open");
                    app.can_close   = bool_field(entry, "close");
                    if (const auto title = string_field(entry, "window_title"); !title.empty()) {
                        app.window_title = title;
                    }
                }
                const std::string label =
                    "программа " + (app.id.empty() ? "#" + std::to_string(index + 1)
                                                   : "«" + app.id + "»");
                if (!entry.is_object()) {
                    warn(label, "это должен быть объект");
                    return std::nullopt;
                }
                if (!is_valid_id(app.id)) {
                    warn(label, "id должен состоять из a-z, 0-9, - и _");
                    return std::nullopt;
                }
                if (app.name.empty()) {
                    warn(label, "нет name");
                    return std::nullopt;
                }
                if (!is_valid_bundle_id(app.bundle_id)) {
                    warn(label, "нужен bundle_id вида com.microsoft.VSCode");
                    return std::nullopt;
                }
                if (const auto open_folder = entry.find("open_folder"); open_folder != entry.end()) {
                    OpenFolderCommand command;
                    if (open_folder->is_object()) {
                        command.cli = string_field(*open_folder, "cli");
                        if (const auto args = open_folder->find("args");
                            args != open_folder->end() && args->is_array()) {
                            for (const auto& arg : *args) {
                                if (arg.is_string()) {
                                    command.args.push_back(arg.get<std::string>());
                                }
                            }
                        }
                    }
                    const fs::path cli(command.cli);
                    const bool     escapes =
                        std::any_of(cli.begin(), cli.end(), [](const fs::path& p) { return p == ".."; });
                    const bool has_path = std::count(command.args.begin(), command.args.end(),
                                                     std::string("{path}")) == 1;
                    if (command.cli.empty() || cli.is_absolute() || escapes) {
                        warn(label, "open_folder.cli — путь внутри .app, например "
                                    "Contents/Resources/app/bin/code");
                        return std::nullopt;
                    }
                    if (!has_path) {
                        warn(label, "в open_folder.args должен быть ровно один \"{path}\"");
                        return std::nullopt;
                    }
                    app.open_folder = std::move(command);
                }
                if (app.window_title.find("{folder}") == std::string::npos) {
                    warn(label, "window_title должен содержать {folder}");
                    return std::nullopt;
                }
                return app;
            }

            std::optional<AccessFolder> folder(const json&                   entry,
                                               std::size_t                   index,
                                               const std::vector<AccessApp>& apps) const {
                AccessFolder folder;
                std::string  raw_path;
                if (entry.is_object()) {
                    folder.id        = string_field(entry, "id");
                    folder.name      = string_field(entry, "name");
                    folder.contains  = string_field(entry, "contains");
                    folder.open_with = string_field(entry, "open_with");
                    raw_path         = string_field(entry, "path");
                }
                const std::string label =
                    "папка " + (folder.id.empty() ? "#" + std::to_string(index + 1)
                                                  : "«" + folder.id + "»");
                if (!entry.is_object()) {
                    warn(label, "это должен быть объект");
                    return std::nullopt;
                }
                if (!is_valid_id(folder.id)) {
                    warn(label, "id должен состоять из a-z, 0-9, - и _");
                    return std::nullopt;
                }
                if (folder.name.empty()) {
                    folder.name = folder.id;
                }
                std::error_code ec;
                const fs::path  resolved = fs::canonical(expand_home(raw_path), ec);
                if (raw_path.empty() || ec || !fs::is_directory(resolved, ec)) {
                    warn(label, "path «" + raw_path + "» не найден или это не папка");
                    return std::nullopt;
                }
                folder.path = resolved;
                if (folder.contains != "projects") {
                    warn(label, "contains пока может быть только \"projects\"");
                    return std::nullopt;
                }
                const auto app = std::find_if(apps.begin(), apps.end(), [&](const AccessApp& a) {
                    return a.id == folder.open_with;
                });
                if (app == apps.end() || !app->open_folder) {
                    warn(label, "open_with должен указывать на программу с open_folder");
                    return std::nullopt;
                }
                return folder;
            }

        private:
            fs::path file_;
        };

    } // namespace

    AccessConfig AccessConfig::load(const fs::path& config_dir) {
        AccessConfig config;

        fs::path      file = config_dir / k_file_name;
        std::ifstream input(file);
        if (!input) {
            const fs::path legacy = config_dir / k_legacy_file_name;
            input.open(legacy);
            if (!input) {
                return config; // no access.json – no access
            }
            term::print_error("файл " + legacy.string() + " устарел: переименуй его в " +
                              k_file_name + " (секция \"apps\" та же).");
            file = legacy;
        }

        json root;
        try {
            root = json::parse(input);
        } catch (const json::parse_error& ex) {
            term::print_error("доступ не загружен: некорректный JSON в " + file.string() + ": " +
                              ex.what());
            return config;
        }
        if (!root.is_object()) {
            term::print_error("доступ не загружен: " + file.string() + " должен быть объектом.");
            return config;
        }

        const Loader loader(file);

        if (const auto apps = root.find("apps"); apps != root.end() && apps->is_array()) {
            for (std::size_t i = 0; i < apps->size(); ++i) {
                auto app = loader.app((*apps)[i], i);
                if (!app) {
                    continue;
                }
                if (config.find_app(app->id) != nullptr) {
                    loader.warn("программа «" + app->id + "»", "такой id уже есть");
                    continue;
                }
                config.apps_.push_back(std::move(*app));
            }
        }

        if (const auto folders = root.find("folders");
            folders != root.end() && folders->is_array()) {
            for (std::size_t i = 0; i < folders->size(); ++i) {
                auto folder = loader.folder((*folders)[i], i, config.apps_);
                if (!folder) {
                    continue;
                }
                const bool duplicate = std::any_of(
                    config.folders_.begin(), config.folders_.end(),
                    [&](const AccessFolder& f) { return f.id == folder->id; });
                if (duplicate) {
                    loader.warn("папка «" + folder->id + "»", "такой id уже есть");
                    continue;
                }
                config.folders_.push_back(std::move(*folder));
            }
        }
        return config;
    }

    const AccessApp* AccessConfig::find_app(const std::string& id) const {
        for (const auto& app : apps_) {
            if (app.id == id) {
                return &app;
            }
        }
        return nullptr;
    }

    bool is_inside(const fs::path& path, const fs::path& root) {
        std::error_code ec;
        const fs::path  real_path = fs::canonical(path, ec);
        if (ec) {
            return false;
        }
        const fs::path real_root = fs::canonical(root, ec);
        if (ec) {
            return false;
        }
        auto       p = real_path.begin();
        const auto r_end = real_root.end();
        for (auto r = real_root.begin(); r != r_end; ++r, ++p) {
            if (p == real_path.end() || *p != *r) {
                return false;
            }
        }
        return p != real_path.end(); // strictly inside, not the root itself
    }

} // namespace yuka
