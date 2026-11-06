#pragma once

#include <memory>

#include "agent/skills.hpp"
#include "tools/tool.hpp"

namespace yuka {

    // The built-in `load_skill(name)` tool: returns the instructions of a skill, so their
    // full text enters the context only when the model needs them.
    // `library` must outlive the tool.
    std::unique_ptr<Tool> make_load_skill_tool(const SkillLibrary& library);

} // namespace yuka
