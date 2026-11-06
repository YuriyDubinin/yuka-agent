#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace yuka {

    // A request from the model to run a tool.
    struct ToolCall {
        std::string    id; // may be empty: not every Ollama version assigns ids
        std::string    name;
        nlohmann::json arguments = nlohmann::json::object();
    };

    struct ChatMessage {
        std::string           role; // "system" | "user" | "assistant" | "tool"
        std::string           content;
        std::vector<ToolCall> tool_calls; // assistant: the tools it asked for
        std::string           tool_name;  // tool: which tool produced this result

        static ChatMessage system(std::string text) { return {"system", std::move(text), {}, {}}; }
        static ChatMessage user(std::string text) { return {"user", std::move(text), {}, {}}; }
        static ChatMessage assistant(std::string text, std::vector<ToolCall> calls = {}) {
            return {"assistant", std::move(text), std::move(calls), {}};
        }
        static ChatMessage tool(std::string name, std::string result) {
            return {"tool", std::move(result), {}, std::move(name)};
        }
    };

    // What the model is told about a tool (Ollama `tools`, OpenAI-style "function").
    struct ToolSpec {
        std::string    name;
        std::string    description;
        nlohmann::json parameters = nlohmann::json::object(); // JSON Schema, type "object"
    };

    // The complete reply of one model call.
    struct ChatReply {
        std::string           text;
        std::vector<ToolCall> tool_calls;
    };

    // Receives each streamed piece of the reply. Return false to stop generation early.
    using ChunkHandler = std::function<bool(const std::string& chunk)>;

    // Minimal client for the Ollama chat API (POST /api/chat), implemented on top of curl.
    class OllamaClient {
    public:
        OllamaClient(std::string    base_url,
                     std::string    model,
                     nlohmann::json options = nlohmann::json::object());

        // Streams the assistant reply through `on_chunk` and returns it with any tool calls.
        // `tools` empty: the request is exactly the plain chat request (no "tools" field).
        // Throws std::runtime_error on transport, HTTP or API errors.
        ChatReply chat(const std::vector<ChatMessage>& messages,
                       const std::vector<ToolSpec>&    tools,
                       const ChunkHandler&             on_chunk) const;

        // Plain chat without tools; returns the reply text.
        std::string chat(const std::vector<ChatMessage>& messages,
                         const ChunkHandler&             on_chunk) const {
            return chat(messages, {}, on_chunk).text;
        }

        // Names of the models installed on the server (GET /api/tags), e.g. "qwen2.5:7b".
        // Throws std::runtime_error if the server is not reachable or answers with an error.
        std::vector<std::string> list_models() const;

        const std::string& model() const { return model_; }
        const std::string& base_url() const { return base_url_; }

    private:
        std::string    base_url_;
        std::string    model_;
        nlohmann::json options_;
    };

} // namespace yuka
