#include "agent/skills.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

#include "platform/terminal.hpp"

namespace yuka {

    namespace {

        using nlohmann::json;

        bool is_valid_name(const std::string& name) {
            if (name.empty() || name.size() > 64) {
                return false;
            }
            return std::all_of(name.begin(), name.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            });
        }

        std::string string_field(const json& object, const char* key) {
            const auto it = object.find(key);
            return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
        }

        // "instructions": a list of lines (like "rules" in default.json) or one string.
        std::string instructions_of(const json& skill) {
            const auto it = skill.find("instructions");
            if (it == skill.end()) {
                return {};
            }
            if (it->is_string()) {
                return it->get<std::string>();
            }
            if (!it->is_array()) {
                return {};
            }
            std::string text;
            for (const auto& line : *it) {
                if (!line.is_string()) {
                    return {}; // reported as "no instructions"
                }
                text += (text.empty() ? "" : "\n") + line.get<std::string>();
            }
            return text;
        }

        void warn(const std::string& message) {
            term::print_error(message);
        }

    } // namespace

    SkillLibrary SkillLibrary::load(const std::filesystem::path& file) {
        SkillLibrary library;

        std::ifstream input(file);
        if (!input) {
            return library; // no skills.json – no skills
        }

        json root;
        try {
            root = json::parse(input);
        } catch (const json::parse_error& ex) {
            warn("навыки не загружены: некорректный JSON в " + file.string() + ": " + ex.what());
            return library;
        }

        const auto list = root.is_object() ? root.find("skills") : root.end();
        if (!root.is_object() || list == root.end() || !list->is_array()) {
            warn("навыки не загружены: в " + file.string() +
                 " нужен объект с массивом \"skills\".");
            return library;
        }

        for (std::size_t i = 0; i < list->size(); ++i) {
            const json& entry = (*list)[i];
            const auto  skip  = [&](const std::string& name, const std::string& why) {
                warn("навык " + (name.empty() ? "#" + std::to_string(i + 1) : "«" + name + "»") +
                     " в " + file.string() + " пропущен: " + why + ".");
            };
            if (!entry.is_object()) {
                skip({}, "это должен быть объект");
                continue;
            }

            Skill skill{string_field(entry, "name"),
                        string_field(entry, "description"),
                        instructions_of(entry)};

            if (!is_valid_name(skill.name)) {
                skip(skill.name, "имя (name) должно состоять из a-z, 0-9, - и _");
                continue;
            }
            if (skill.description.empty()) {
                skip(skill.name, "нет описания (description)");
                continue;
            }
            if (skill.instructions.empty()) {
                skip(skill.name, "нет инструкции (instructions — список строк)");
                continue;
            }
            if (library.find(skill.name) != nullptr) {
                skip(skill.name, "навык с таким именем уже есть");
                continue;
            }
            library.skills_.push_back(std::move(skill));
        }
        return library;
    }

    const Skill* SkillLibrary::find(const std::string& name) const {
        for (const auto& skill : skills_) {
            if (skill.name == name) {
                return &skill;
            }
        }
        return nullptr;
    }

} // namespace yuka
