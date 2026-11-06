#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace yuka {

    // A skill: written knowledge of how to do something well ("how to talk about the
    // interface", "how to plan Yura's morning"). Not code – instructions for the model.
    //
    // All skills live in one file, config/skills.json, in the style of default.json:
    //
    //   {
    //     "skills": [
    //       {
    //         "name": "interface-help",
    //         "description": "Как рассказать Юре о своём интерфейсе",
    //         "instructions": ["первая строка", "вторая строка", "..."]
    //       }
    //     ]
    //   }
    //
    // Only names and descriptions go into the system prompt; the instructions are loaded on
    // demand by the built-in `load_skill` tool (progressive disclosure).
    struct Skill {
        std::string name;
        std::string description;
        std::string instructions; // the lines joined with '\n'
    };

    class SkillLibrary {
    public:
        inline static const char* const k_file_name = "skills.json";

        // Loads `file`. A missing file means no skills; a broken file or broken skills are
        // skipped with a warning – the agent starts anyway.
        static SkillLibrary load(const std::filesystem::path& file);

        bool                      empty() const { return skills_.empty(); }
        const std::vector<Skill>& skills() const { return skills_; }
        const Skill*              find(const std::string& name) const;

    private:
        std::vector<Skill> skills_; // in file order
    };

} // namespace yuka
