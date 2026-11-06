#include "tools/builtin/load_skill_tool.hpp"

namespace yuka {

    namespace {

        using nlohmann::json;

        class LoadSkillTool final : public Tool {
        public:
            explicit LoadSkillTool(const SkillLibrary& library) : library_(library) {
                json names = json::array();
                for (const auto& skill : library.skills()) {
                    names.push_back(skill.name);
                }
                spec_.name        = "load_skill";
                spec_.description = "Загружает подробную инструкцию навыка из раздела «Навыки». "
                                    "Вызывай перед задачей, которая подходит под навык.";
                spec_.parameters  = {
                    {"type", "object"},
                    {"properties",
                     {{"name",
                       {{"type", "string"}, {"enum", names}, {"description", "Имя навыка"}}}}},
                    {"required", json::array({"name"})},
                    {"additionalProperties", false},
                };
            }

            const ToolSpec&    spec() const override { return spec_; }
            const std::string& title() const override { return title_; }
            ToolAnnotations    annotations() const override { return {true, false, true}; }

            ToolResult call(const json& arguments) override {
                const auto name = arguments.find("name");
                if (name == arguments.end() || !name->is_string()) {
                    return ToolResult::error("Укажи имя навыка в поле name.");
                }
                const Skill* skill = library_.find(name->get<std::string>());
                if (skill == nullptr) {
                    return ToolResult::error("Навыка «" + name->get<std::string>() + "» нет.");
                }
                return ToolResult::ok("Навык «" + skill->name + "»:\n\n" + skill->instructions);
            }

        private:
            const SkillLibrary& library_;
            ToolSpec            spec_;
            std::string         title_ = "Загрузить навык";
        };

    } // namespace

    std::unique_ptr<Tool> make_load_skill_tool(const SkillLibrary& library) {
        return std::make_unique<LoadSkillTool>(library);
    }

} // namespace yuka
