#pragma once

#include <memory>
#include <vector>

#include "agent/access.hpp"
#include "tools/tool.hpp"

namespace yuka {

    // `app_open` / `app_close` for the apps in config/access.json that allow them (none if
    // nothing is allowed). Only listed apps exist for the model: the tools accept nothing
    // but their ids. `access` must outlive the tools.
    std::vector<std::unique_ptr<Tool>> make_app_tools(const AccessConfig& access);

} // namespace yuka
