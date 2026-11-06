#include "tools/builtin/app_tools.hpp"

#include <functional>
#include <nlohmann/json.hpp>

#include "platform/macos_apps.hpp"

namespace yuka {

    namespace {

        using nlohmann::json;

        ToolResult open_app(const AccessApp& app) {
            if (!macos::open_bundle(app.bundle_id)) {
                return ToolResult::error("Не удалось открыть " + app.name +
                                         ". Возможно, программа не установлена.");
            }
            return ToolResult::ok(app.name + " открыт.");
        }

        ToolResult close_app(const AccessApp& app) {
            switch (macos::quit(app.bundle_id)) {
                case macos::QuitResult::quit:
                    return ToolResult::ok(app.name + " закрыт.");
                case macos::QuitResult::not_running:
                    return ToolResult::ok(app.name + " и так не запущен.");
                case macos::QuitResult::waiting_for_user:
                    return ToolResult::ok(app.name +
                                          " закрывается и, похоже, спрашивает про несохранённые "
                                          "файлы — ответь в его окне.");
                case macos::QuitResult::failed:
                    break;
            }
            return ToolResult::error("Не удалось закрыть " + app.name + ".");
        }

        // One tool over the subset of apps that allow an action.
        class AppActionTool final : public Tool {
        public:
            using Action = std::function<ToolResult(const AccessApp&)>;

            AppActionTool(std::vector<const AccessApp*> apps,
                          std::string                   name,
                          std::string                   title,
                          const std::string&            what,
                          Action                        action)
                : apps_(std::move(apps)), title_(std::move(title)), action_(std::move(action)) {
                json        ids = json::array();
                std::string list;
                for (const AccessApp* app : apps_) {
                    ids.push_back(app->id);
                    list += "\n- " + app->id + " — " + app->name +
                            (app->description.empty() ? "" : ": " + app->description);
                }
                spec_.name        = std::move(name);
                spec_.description = what + " Доступны только эти программы:" + list;
                spec_.parameters  = {
                    {"type", "object"},
                    {"properties", {{"app", {{"type", "string"}, {"enum", ids}}}}},
                    {"required", json::array({"app"})},
                    {"additionalProperties", false},
                };
            }

            const ToolSpec&    spec() const override { return spec_; }
            const std::string& title() const override { return title_; }
            ToolAnnotations    annotations() const override { return {false, false, true}; }

            ToolResult call(const json& arguments) override {
                const auto        it = arguments.find("app");
                const std::string id =
                    it != arguments.end() && it->is_string() ? it->get<std::string>() : "";
                for (const AccessApp* app : apps_) {
                    if (app->id == id) {
                        return action_(*app);
                    }
                }
                std::string allowed;
                for (const AccessApp* app : apps_) {
                    allowed += (allowed.empty() ? "" : ", ") + app->id;
                }
                return ToolResult::error("Программы «" + id + "» нет в разрешённом списке (" +
                                         allowed + ").");
            }

        private:
            std::vector<const AccessApp*> apps_;
            ToolSpec                      spec_;
            std::string                   title_;
            Action                        action_;
        };

    } // namespace

    std::vector<std::unique_ptr<Tool>> make_app_tools(const AccessConfig& access) {
        std::vector<const AccessApp*> openable;
        std::vector<const AccessApp*> closable;
        for (const auto& app : access.apps()) {
            if (app.can_open) {
                openable.push_back(&app);
            }
            if (app.can_close) {
                closable.push_back(&app);
            }
        }

        std::vector<std::unique_ptr<Tool>> tools;
        if (!openable.empty()) {
            tools.push_back(std::make_unique<AppActionTool>(
                std::move(openable),
                "app_open",
                "Открыть программу",
                "Открывает программу на компьютере Юры (или переводит её на передний план). "
                "Вызывай, когда Юра просит открыть или запустить программу. Чтобы открыть "
                "проект, используй project_open.",
                open_app));
        }
        if (!closable.empty()) {
            tools.push_back(std::make_unique<AppActionTool>(
                std::move(closable),
                "app_close",
                "Закрыть программу",
                "Закрывает программу целиком, как ⌘Q: о несохранённом программа спросит сама. "
                "Вызывай, только когда Юра прямо просит закрыть саму программу. Чтобы закрыть "
                "проект, используй project_close.",
                close_app));
        }
        return tools;
    }

} // namespace yuka
