#include "tools/builtin/project_tools.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>

#include "platform/macos_apps.hpp"
#include "platform/macos_windows.hpp"
#include "platform/process.hpp"
#include "tools/builtin/name_match.hpp"

namespace yuka {

    namespace {

        namespace fs = std::filesystem;
        using nlohmann::json;
        using namespace std::chrono_literals;

        // Above this many projects their names are not spelled out in the tool description.
        constexpr std::size_t k_max_listed_projects = 40;

        struct Project {
            std::string         name;  // folder name, e.g. "Alfa"
            std::string         label; // what the model sees; "Alfa (Проекты)" if not unique
            fs::path            path;
            const AccessFolder* folder;
            const AccessApp*    app;
        };

        std::string join(const std::vector<std::string>& items) {
            std::string out;
            for (const auto& item : items) {
                out += (out.empty() ? "" : ", ") + item;
            }
            return out;
        }

        std::string lower_ascii(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            });
            return s;
        }

        // The window title VS Code shows for a folder: "file — Alfa" or "Alfa" (sometimes
        // with " — Visual Studio Code" at the end). A window belongs to the project when one
        // of its " — " parts equals the app's window_title with {folder} filled in.
        bool window_belongs_to(const std::string& title, const AccessApp& app, const std::string& folder) {
            std::string expected = app.window_title;
            const auto  at       = expected.find("{folder}");
            expected.replace(at, 8, folder);

            const std::string separator = " \xE2\x80\x94 "; // " — "
            std::size_t       start     = 0;
            while (true) {
                const auto  end  = title.find(separator, start);
                std::string part = title.substr(start, end == std::string::npos ? end : end - start);
                if (part.rfind("\xE2\x97\x8F ", 0) == 0) { // "● " – unsaved changes marker
                    part = part.substr(4);
                }
                if (part == expected) {
                    return true;
                }
                if (end == std::string::npos) {
                    return false;
                }
                start = end + separator.size();
            }
        }

        // All projects in the allowed folders, read from disk now.
        class ProjectCatalog {
        public:
            explicit ProjectCatalog(const AccessConfig& access) {
                for (const auto& folder : access.folders()) {
                    if (folder.contains == "projects") {
                        if (const AccessApp* app = access.find_app(folder.open_with)) {
                            roots_.push_back({&folder, app});
                        }
                    }
                }
            }

            bool empty() const { return roots_.empty(); }

            std::vector<Project> list() const {
                std::vector<Project> projects;
                for (const auto& [folder, app] : roots_) {
                    std::error_code ec;
                    for (const auto& entry : fs::directory_iterator(folder->path, ec)) {
                        const std::string name = entry.path().filename().string();
                        if (name.empty() || name[0] == '.' || !entry.is_directory(ec)) {
                            continue; // hidden folders and files are not projects
                        }
                        if (!is_inside(entry.path(), folder->path)) {
                            continue; // a symlink that leads outside the allowed folder
                        }
                        projects.push_back({name, name, entry.path(), folder, app});
                    }
                }
                std::sort(projects.begin(), projects.end(), [](const Project& a, const Project& b) {
                    return lower_ascii(a.name) < lower_ascii(b.name);
                });
                // The same name in two allowed folders: tell them apart by the folder.
                for (auto& p : projects) {
                    const auto same = std::count_if(projects.begin(), projects.end(),
                                                    [&](const Project& o) { return o.name == p.name; });
                    if (same > 1) {
                        p.label = p.name + " (" + p.folder->name + ")";
                    }
                }
                return projects;
            }

            std::string where() const {
                std::vector<std::string> paths;
                for (const auto& root : roots_) {
                    paths.push_back(root.first->path.string());
                }
                return join(paths);
            }

        private:
            std::vector<std::pair<const AccessFolder*, const AccessApp*>> roots_;
        };

        // Resolves what Yura said to one project, or explains to the model why not.
        struct Lookup {
            std::optional<Project> project;
            std::string            problem;
        };

        Lookup find_project(const ProjectCatalog& catalog, const std::string& query) {
            const auto               projects = catalog.list();
            std::vector<std::string> labels;
            for (const auto& p : projects) {
                labels.push_back(p.label);
            }
            if (projects.empty()) {
                return {std::nullopt, "В папке проектов (" + catalog.where() + ") нет проектов."};
            }

            const NameMatch match = match_name(query, labels);
            switch (match.kind) {
                case NameMatch::Kind::found:
                    for (const auto& p : projects) {
                        if (p.label == match.name) {
                            return {p, {}};
                        }
                    }
                    break;
                case NameMatch::Kind::ambiguous:
                    return {std::nullopt, "Под «" + query + "» подходят несколько проектов: " +
                                              join(match.candidates) + ". Уточни у Юры, какой."};
                case NameMatch::Kind::not_found:
                    break;
            }
            return {std::nullopt, "Проекта «" + query + "» нет. Похожие: " +
                                      join(match.candidates) + ". Все проекты: " +
                                      join(labels) + "."};
        }

        std::string name_argument(const json& arguments) {
            const auto it = arguments.find("name");
            return it != arguments.end() && it->is_string() ? it->get<std::string>() : "";
        }

        // Shared plumbing of the three tools: spec with the current project list.
        class ProjectTool : public Tool {
        public:
            ProjectTool(std::shared_ptr<const ProjectCatalog> catalog,
                        std::string                           name,
                        std::string                           title,
                        std::string                           what,
                        bool                                  takes_name)
                : catalog_(std::move(catalog)),
                  title_(std::move(title)),
                  what_(std::move(what)),
                  takes_name_(takes_name) {
                spec_.name = std::move(name);
                refresh();
            }

            const ToolSpec&    spec() const override { return spec_; }
            const std::string& title() const override { return title_; }
            ToolAnnotations    annotations() const override { return {false, false, true}; }

            void refresh() override {
                std::string description = what_;
                if (takes_name_) {
                    const auto projects = catalog_->list();
                    if (!projects.empty() && projects.size() <= k_max_listed_projects) {
                        std::vector<std::string> labels;
                        for (const auto& p : projects) {
                            labels.push_back(p.label);
                        }
                        description += " Проекты: " + join(labels) + ".";
                    }
                    spec_.parameters = {
                        {"type", "object"},
                        {"properties",
                         {{"name",
                           {{"type", "string"},
                            {"description", "Имя проекта — как его назвал Юра или из списка"}}}}},
                        {"required", json::array({"name"})},
                        {"additionalProperties", false},
                    };
                } else {
                    spec_.parameters = {{"type", "object"},
                                        {"properties", json::object()},
                                        {"additionalProperties", false}};
                }
                spec_.description = std::move(description);
            }

        protected:
            const ProjectCatalog& catalog() const { return *catalog_; }

        private:
            std::shared_ptr<const ProjectCatalog> catalog_;
            ToolSpec                              spec_;
            std::string                           title_;
            std::string                           what_;
            bool                                  takes_name_;
        };

        class ProjectOpenTool final : public ProjectTool {
        public:
            explicit ProjectOpenTool(std::shared_ptr<const ProjectCatalog> catalog)
                : ProjectTool(std::move(catalog),
                              "project_open",
                              "Открыть проект",
                              "Открывает проект (папку) в редакторе в новом окне; если он уже "
                              "открыт, редактор сам переключит на его окно. Вызывай на «открой "
                              "проект …» каждый раз, даже если проект открывали раньше.",
                              true) {}

            ToolResult call(const json& arguments) override {
                const Lookup lookup = find_project(catalog(), name_argument(arguments));
                if (!lookup.project) {
                    return ToolResult::error(lookup.problem);
                }
                const Project&   p   = *lookup.project;
                const AccessApp& app = *p.app;
                if (!is_inside(p.path, p.folder->path)) {
                    return ToolResult::error("Папка проекта вне разрешённой папки.");
                }

                // Whether the project is already open is decided by the editor itself, not by
                // window titles (a leftover window may still carry the project's name): VS Code
                // opens the folder in a new window, or focuses the window that really has it.
                const auto bundle = macos::bundle_path(app.bundle_id);
                if (!bundle) {
                    return ToolResult::error(app.name + " не найден на компьютере.");
                }
                std::vector<std::string> argv{(*bundle / app.open_folder->cli).string()};
                for (const auto& arg : app.open_folder->args) {
                    argv.push_back(arg == "{path}" ? p.path.string() : arg);
                }
                const CommandResult result = run_command(argv, 30s);
                if (result.exit_code != 0) {
                    return ToolResult::error("Не удалось открыть проект " + p.name + " в " +
                                             app.name + ".");
                }
                return ToolResult::ok("Открыла проект " + p.name + " в " + app.name + ".");
            }
        };

        class ProjectCloseTool final : public ProjectTool {
        public:
            explicit ProjectCloseTool(std::shared_ptr<const ProjectCatalog> catalog)
                : ProjectTool(std::move(catalog),
                              "project_close",
                              "Закрыть проект",
                              "Закрывает окно редактора с этим проектом; сам редактор и другие "
                              "проекты остаются открытыми. Вызывай на «закрой проект …».",
                              true) {}

            ToolResult call(const json& arguments) override {
                const Lookup lookup = find_project(catalog(), name_argument(arguments));
                if (!lookup.project) {
                    return ToolResult::error(lookup.problem);
                }
                const Project&   p   = *lookup.project;
                const AccessApp& app = *p.app;

                const macos::WindowList windows = macos::window_titles(app.bundle_id);
                if (windows.status != macos::WindowStatus::ok) {
                    return ToolResult::error(macos::permission_hint(windows.status));
                }

                std::set<std::string> titles; // close each distinct title once
                for (const auto& title : windows.titles) {
                    if (window_belongs_to(title, app, p.name)) {
                        titles.insert(title);
                    }
                }
                if (titles.empty()) {
                    return ToolResult::ok("Проект " + p.name +
                                          " не открыт, поэтому закрыть его нельзя.");
                }

                int total = 0;
                for (const auto& title : titles) {
                    int        closed = 0;
                    const auto status = macos::close_windows(app.bundle_id, title, closed);
                    if (status != macos::WindowStatus::ok) {
                        return ToolResult::error(macos::permission_hint(status));
                    }
                    total += closed;
                }
                if (total == 0) {
                    return ToolResult::error("Не удалось закрыть окно проекта " + p.name + ".");
                }
                return ToolResult::ok("Закрыла проект " + p.name +
                                      ". Если там были несохранённые файлы, " + app.name +
                                      " спросит про них.");
            }
        };

        class ProjectCloseAllTool final : public ProjectTool {
        public:
            explicit ProjectCloseAllTool(std::shared_ptr<const ProjectCatalog> catalog)
                : ProjectTool(std::move(catalog),
                              "project_close_all",
                              "Закрыть все проекты",
                              "Закрывает окна всех открытых проектов из папки проектов; другие "
                              "окна редактора не трогает. Вызывай на «закрой все проекты».",
                              false) {}

            ToolResult call(const json& /*arguments*/) override {
                const auto projects = catalog().list();

                // Group by app: one window list per editor.
                std::vector<const AccessApp*> apps;
                for (const auto& p : projects) {
                    if (std::find(apps.begin(), apps.end(), p.app) == apps.end()) {
                        apps.push_back(p.app);
                    }
                }

                std::vector<std::string> closed_names;
                for (const AccessApp* app : apps) {
                    const macos::WindowList windows = macos::window_titles(app->bundle_id);
                    if (windows.status != macos::WindowStatus::ok) {
                        return ToolResult::error(macos::permission_hint(windows.status));
                    }
                    std::set<std::string> done;
                    for (const auto& title : windows.titles) {
                        for (const auto& p : projects) {
                            if (p.app != app || !window_belongs_to(title, *app, p.name) ||
                                done.count(title) != 0) {
                                continue;
                            }
                            done.insert(title);
                            int        closed = 0;
                            const auto status = macos::close_windows(app->bundle_id, title, closed);
                            if (status != macos::WindowStatus::ok) {
                                return ToolResult::error(macos::permission_hint(status));
                            }
                            if (closed > 0 &&
                                std::find(closed_names.begin(), closed_names.end(), p.name) ==
                                    closed_names.end()) {
                                closed_names.push_back(p.name);
                            }
                        }
                    }
                }

                if (closed_names.empty()) {
                    return ToolResult::ok("Открытых проектов нет — закрывать нечего.");
                }
                return ToolResult::ok("Закрыла проекты: " + join(closed_names) + ".");
            }
        };

    } // namespace

    std::vector<std::unique_ptr<Tool>> make_project_tools(const AccessConfig& access) {
        auto catalog = std::make_shared<const ProjectCatalog>(access);
        std::vector<std::unique_ptr<Tool>> tools;
        if (catalog->empty()) {
            return tools;
        }
        tools.push_back(std::make_unique<ProjectOpenTool>(catalog));
        tools.push_back(std::make_unique<ProjectCloseTool>(catalog));
        tools.push_back(std::make_unique<ProjectCloseAllTool>(catalog));
        return tools;
    }

} // namespace yuka
