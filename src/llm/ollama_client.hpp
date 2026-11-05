#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace yuka {

    struct ChatMessage {
        std::string role; // "system" | "user" | "assistant"
        std::string content;
    };

    // Receives each streamed piece of the reply. Return false to stop generation early.
    using ChunkHandler = std::function<bool(const std::string& chunk)>;

    // Minimal client for the Ollama chat API (POST /api/chat), implemented on top of curl.
    class OllamaClient {
    public:
        OllamaClient(std::string    base_url,
                     std::string    model,
                     nlohmann::json options = nlohmann::json::object());

        // Streams the assistant reply for the given conversation through `on_chunk` and
        // returns the full text. Throws std::runtime_error on transport, HTTP or API errors.
        std::string chat(const std::vector<ChatMessage>& messages,
                         const ChunkHandler&             on_chunk) const;

        // Names of the models installed on the server (GET /api/tags), e.g. "qwen2.5:7b".
        // Throws std::runtime_error if the server is not reachable or answers with an error.
        std::vector<std::string> list_models() const;

        const std::string& model() const { return model_; }

    private:
        std::string    base_url_;
        std::string    model_;
        nlohmann::json options_;
    };

} // namespace yuka
