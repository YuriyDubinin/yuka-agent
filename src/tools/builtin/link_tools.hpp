#pragma once

#include <memory>
#include <vector>

#include "agent/access.hpp"
#include "tools/tool.hpp"

namespace yuka {

    // `link_open(name)`: opens a page from "links" in config/access.json in its browser
    // ("открой ютуб" → https://www.youtube.com in Google Chrome). The model names the page;
    // the address always comes from the file, never from the model. None if there are no
    // links. `access` must outlive the tools.
    std::vector<std::unique_ptr<Tool>> make_link_tools(const AccessConfig& access);

} // namespace yuka
