#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "llm/ollama_client.hpp"

namespace yuka {

    // Hints about what a tool does (names as in MCP `annotations`).
    struct ToolAnnotations {
        bool read_only   = false; // changes nothing
        bool destructive = false; // irreversible or very noticeable: needs the user's "yes"
        bool idempotent  = false; // repeating it with the same arguments changes nothing
    };

    // What a tool returns to the model. `is_error`: the call failed and `text` says why,
    // so the model can fix the call or tell the user.
    struct ToolResult {
        std::string text;
        bool        is_error = false;

        static ToolResult ok(std::string text) { return {std::move(text), false}; }
        static ToolResult error(std::string text) { return {std::move(text), true}; }
    };

    // Something Yuka can do besides talking. Built into the agent (load_skill) or provided
    // by the connected face (see RemoteTool).
    class Tool {
    public:
        virtual ~Tool() = default;

        // Name, description for the model and JSON Schema of the arguments.
        virtual const ToolSpec& spec() const = 0;

        // Short human-readable name for status lines ("Сменить тему").
        virtual const std::string& title() const = 0;

        virtual ToolAnnotations annotations() const = 0;

        // Runs the tool. `arguments` is always a JSON object.
        virtual ToolResult call(const nlohmann::json& arguments) = 0;
    };

} // namespace yuka
