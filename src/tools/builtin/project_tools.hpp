#pragma once

#include <memory>
#include <vector>

#include "agent/access.hpp"
#include "tools/tool.hpp"

namespace yuka {

    // Projects: every subfolder of a folder with "contains": "projects" in access.json,
    // opened with its "open_with" app (VS Code).
    //
    //   project_open(name)   – opens the project in a new window (or raises its window)
    //   project_close(name)  – closes the windows of that project; the editor keeps running
    //   project_close_all()  – closes the windows of every open project from these folders
    //
    // The list of projects is read from disk before every turn, so new folders appear
    // without a restart. Names are matched the way Yura says them ("открой альфу" → Alfa);
    // an unclear name is never guessed – the model gets the candidates and asks.
    // `access` must outlive the tools.
    std::vector<std::unique_ptr<Tool>> make_project_tools(const AccessConfig& access);

} // namespace yuka
