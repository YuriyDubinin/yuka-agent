#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "llm/ollama_client.hpp"

namespace yuka {

    // Small models sometimes *write* a tool call as text instead of using the tool-calling
    // mechanism, e.g.
    //   <tool_call>{"name": "ui_set_theme", "arguments": {"mode": "night"}}</tool_call>
    //   ```json\n{"name": "ui_set_theme", "arguments": {...}}\n```
    // This recognises such text (one call or a JSON array of calls) for tools that exist.
    // Anything else – including ordinary prose that merely mentions a tool – is not a call.
    std::optional<std::vector<ToolCall>>
    parse_textual_tool_calls(const std::string&                             text,
                             const std::function<bool(const std::string&)>& tool_exists);

    // True if a reply that starts like `text` might be a written tool call, so its streaming
    // should be held back until the reply is complete.
    bool may_be_textual_tool_call(const std::string& text);

} // namespace yuka
