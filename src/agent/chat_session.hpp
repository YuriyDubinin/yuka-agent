#pragma once

#include <string>
#include <vector>

#include "agent/config.hpp"
#include "agent/skills.hpp"
#include "faces/face.hpp"
#include "llm/ollama_client.hpp"
#include "tools/tool_registry.hpp"

namespace yuka {

    // One conversation with Yuka through a face.
    //
    // A turn turns one user message into one reply. The model may call tools on the way:
    //   model → tool_calls → run them (agent's own or the face's) → results → model …
    // at most k_max_model_calls times. The face sees the whole turn as a single reply.
    // A failed or interrupted turn leaves the history exactly as it was before the turn.
    class ChatSession {
    public:
        static constexpr std::size_t k_max_model_calls = 4;
        // User/assistant/tool messages kept in context (the system prompt is always kept).
        static constexpr std::size_t k_max_history_messages = 40;

        ChatSession(Face&                 face,
                    const SessionContext& session,
                    const OllamaClient&   client,
                    const AgentConfig&    config,
                    SkillLibrary          skills,
                    bool                  report_tools_in_terminal);

        // Greets and runs turns until the user leaves, the face closes or Ctrl+C.
        void run();

    private:
        void refresh_tools();
        void run_turn(const std::string& input);
        void trim_history();
        void report_tool(const Tool* tool, const ToolCall& call) const;

        Face&                 face_;
        const SessionContext& session_;
        const OllamaClient&   client_;
        const AgentConfig&    config_;
        SkillLibrary          skills_;
        ToolRegistry          tools_;
        bool                  report_tools_;
        std::string           greeting_;
        std::vector<ChatMessage> history_;
        int                      next_call_id_ = 0;
    };

} // namespace yuka
