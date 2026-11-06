#pragma once

#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "tools/tool.hpp"

namespace yuka {

    struct AgentInfo {
        std::string name;
        std::string model;
    };

    // A tool the face offers (protocol v2 `capabilities`).
    struct FaceTool {
        ToolSpec        spec;
        std::string     title;
        ToolAnnotations annotations;
    };

    // A "face" is the user-facing side of Yuka: the terminal, or a GUI connected over a socket.
    // The chat loop talks only to this interface.
    class Face {
    public:
        virtual ~Face() = default;

        // Starts the session and shows the greeting.
        virtual void start_session(const AgentInfo& agent, const std::string& greeting) = 0;

        // Blocks until the user sends a message. std::nullopt ends the session.
        virtual std::optional<std::string> read_user_message() = 0;

        // A streamed reply: begin_reply, then reply_chunk* and finally end_reply.
        virtual void begin_reply()                        = 0;
        virtual void reply_chunk(const std::string& text) = 0;
        virtual void end_reply()                          = 0;

        virtual void show_error(const std::string& message) = 0;

        // False once the face is gone (e.g. the window was closed).
        virtual bool is_alive() const = 0;

        // ---- Tools (protocol v2). The terminal offers none.

        // True once after the face (re)sent its tool set; the caller then re-reads tools().
        virtual bool take_tools_changed() { return false; }

        virtual std::vector<FaceTool> tools() const { return {}; }

        // The face's instructions for the model on how to use its tools.
        virtual std::string tool_instructions() const { return {}; }

        // Asks the face to run one of its tools and waits for the result (or the timeout).
        virtual ToolResult call_tool(const std::string& /*name*/,
                                     const nlohmann::json& /*arguments*/,
                                     std::chrono::milliseconds /*timeout*/) {
            return ToolResult::error("У этого интерфейса нет инструментов.");
        }
    };

} // namespace yuka
