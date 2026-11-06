#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tools/tool.hpp"

namespace yuka {

    // Where a tool comes from. Built-in tools win name collisions.
    enum class ToolSource { builtin, face };

    // The tools offered to the model in this session.
    class ToolRegistry {
    public:
        // Atomically replaces every tool of `source` (e.g. when the face re-sends its set).
        // Face tools whose name is taken by a built-in tool are dropped with a warning.
        void replace_source(ToolSource source, std::vector<std::unique_ptr<Tool>> tools);

        bool empty() const { return tools_.empty(); }

        // Specs for the model: built-in tools first, then the face's, each in its own order.
        std::vector<ToolSpec> specs() const;

        Tool* find(const std::string& name) const;

        // Runs a tool. Never throws: an unknown tool, a non-object argument or an exception
        // inside the tool become an error result the model can read. Results are capped.
        ToolResult call(const std::string& name, const nlohmann::json& arguments) const;

        // Upper bound for a tool result fed back to the model.
        static constexpr std::size_t k_max_result_bytes = 8 * 1024;

    private:
        struct Entry {
            ToolSource            source;
            std::unique_ptr<Tool> tool;
        };
        std::vector<Entry> tools_; // registration order, built-in first
    };

} // namespace yuka
