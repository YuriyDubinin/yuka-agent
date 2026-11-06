#include "llm/ollama_client.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

#include "platform/shell.hpp"

namespace yuka {

    namespace {

        using nlohmann::json;

        // Temporary file holding the request body; removed on scope exit.
        // The body goes through a file (curl --data-binary @file) so that user text never
        // ends up in the shell command line.
        class TempRequestFile {
        public:
            explicit TempRequestFile(const std::string& content) {
                std::string templ =
                    (std::filesystem::temp_directory_path() / "yuka-request-XXXXXX").string();

                const int fd = mkstemp(templ.data());
                if (fd == -1) {
                    throw std::runtime_error("failed to create temporary request file");
                }
                path_ = templ;

                const char* data      = content.data();
                std::size_t remaining = content.size();
                while (remaining > 0) {
                    const ssize_t written = write(fd, data, remaining);
                    if (written <= 0) {
                        close(fd);
                        std::filesystem::remove(path_);
                        throw std::runtime_error("failed to write temporary request file");
                    }
                    data += written;
                    remaining -= static_cast<std::size_t>(written);
                }
                close(fd);
            }

            ~TempRequestFile() {
                std::error_code ec;
                std::filesystem::remove(path_, ec);
            }

            TempRequestFile(const TempRequestFile&)            = delete;
            TempRequestFile& operator=(const TempRequestFile&) = delete;

            const std::string& path() const { return path_; }

        private:
            std::string path_;
        };

        // Owns a popen() stream; closes it on scope exit.
        class Pipe {
        public:
            explicit Pipe(const std::string& command) : pipe_(popen(command.c_str(), "r")) {
                if (pipe_ == nullptr) {
                    throw std::runtime_error("failed to start curl process");
                }
            }

            ~Pipe() { close(); }

            Pipe(const Pipe&)            = delete;
            Pipe& operator=(const Pipe&) = delete;

            // Reads one line (without '\n') of any length. Returns false at end of stream.
            bool read_line(std::string& line) {
                line.clear();
                std::array<char, 4096> buffer{};
                while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe_) !=
                       nullptr) {
                    line += buffer.data();
                    if (!line.empty() && line.back() == '\n') {
                        line.pop_back();
                        return true;
                    }
                }
                return !line.empty();
            }

            // Waits for the process and returns its exit code (-1 if unknown).
            int close() {
                if (pipe_ == nullptr) {
                    return exit_code_;
                }
                const int status = pclose(pipe_);
                pipe_            = nullptr;
                exit_code_       = (status == -1) ? -1 : WEXITSTATUS(status);
                return exit_code_;
            }

        private:
            FILE* pipe_      = nullptr;
            int   exit_code_ = -1;
        };

    } // namespace

    namespace {

        json encode_message(const ChatMessage& message) {
            json out{{"role", message.role}, {"content", message.content}};
            if (!message.tool_calls.empty()) {
                json calls = json::array();
                for (const auto& call : message.tool_calls) {
                    calls.push_back(
                        {{"function", {{"name", call.name}, {"arguments", call.arguments}}}});
                }
                out["tool_calls"] = std::move(calls);
            }
            if (!message.tool_name.empty()) {
                out["tool_name"] = message.tool_name;
            }
            return out;
        }

        json encode_tool(const ToolSpec& tool) {
            return {{"type", "function"},
                    {"function",
                     {{"name", tool.name},
                      {"description", tool.description},
                      {"parameters", tool.parameters}}}};
        }

        // message.tool_calls[] of a streamed chunk. Arguments may be an object or a JSON string.
        void collect_tool_calls(const json& message, std::vector<ToolCall>& out) {
            const auto calls = message.find("tool_calls");
            if (calls == message.end() || !calls->is_array()) {
                return;
            }
            for (const auto& entry : *calls) {
                if (!entry.is_object()) {
                    continue;
                }
                const auto function = entry.find("function");
                if (function == entry.end() || !function->is_object()) {
                    continue;
                }
                ToolCall call;
                if (const auto id = entry.find("id"); id != entry.end() && id->is_string()) {
                    call.id = id->get<std::string>();
                }
                if (const auto name = function->find("name");
                    name != function->end() && name->is_string()) {
                    call.name = name->get<std::string>();
                }
                if (const auto args = function->find("arguments"); args != function->end()) {
                    call.arguments = args->is_string()
                                         ? json::parse(args->get<std::string>(), nullptr, false)
                                         : *args;
                    if (call.arguments.is_discarded() || !call.arguments.is_object()) {
                        call.arguments = json::object();
                    }
                }
                if (!call.name.empty()) {
                    out.push_back(std::move(call));
                }
            }
        }

    } // namespace

    OllamaClient::OllamaClient(std::string base_url, std::string model, nlohmann::json options)
        : base_url_(std::move(base_url)), model_(std::move(model)), options_(std::move(options)) {}

    ChatReply OllamaClient::chat(const std::vector<ChatMessage>& messages,
                                 const std::vector<ToolSpec>&    tools,
                                 const ChunkHandler&             on_chunk) const {
        json body;
        body["model"]  = model_;
        body["stream"] = true;
        if (!options_.empty()) {
            body["options"] = options_;
        }
        body["messages"] = json::array();
        for (const auto& message : messages) {
            body["messages"].push_back(encode_message(message));
        }
        if (!tools.empty()) {
            body["tools"] = json::array();
            for (const auto& tool : tools) {
                body["tools"].push_back(encode_tool(tool));
            }
        }

        // Invalid UTF-8 (e.g. from a face) is replaced instead of aborting the request.
        const TempRequestFile request(body.dump(-1, ' ', false, json::error_handler_t::replace));

        // -sS: no progress bar, but still report transport errors.
        // -N:  disable output buffering so chunks arrive as soon as they are generated.
        // stderr is folded into stdout so curl errors reach the user.
        const std::string command = "curl -sSN --max-time 600 "
                                    "-H 'Content-Type: application/json' "
                                    "--data-binary @'" +
                                    shell_escape_single_quotes(request.path()) + "' '" +
                                    shell_escape_single_quotes(base_url_ + "/api/chat") + "' 2>&1";

        Pipe pipe(command);

        // Ollama streams NDJSON: {"message":{"content":"...","tool_calls":[...]},"done":false}
        // ... {"done":true}. Tool calls arrive in one of the chunks, usually with no text.
        ChatReply   reply;
        std::string diagnostics; // non-JSON output (curl errors)
        std::string line;
        bool        finished = false;
        while (!finished && pipe.read_line(line)) {
            if (line.empty()) {
                continue;
            }

            const json event = json::parse(line, nullptr, /*allow_exceptions=*/false);
            if (event.is_discarded() || !event.is_object()) {
                diagnostics += line + "\n";
                continue;
            }

            if (const auto err = event.find("error"); err != event.end() && err->is_string()) {
                throw std::runtime_error("Ollama: " + err->get<std::string>());
            }

            if (const auto message = event.find("message");
                message != event.end() && message->is_object()) {
                collect_tool_calls(*message, reply.tool_calls);

                const auto content = message->find("content");
                if (content != message->end() && content->is_string()) {
                    const std::string chunk = content->get<std::string>();
                    if (!chunk.empty()) {
                        reply.text += chunk;
                        if (!on_chunk(chunk)) {
                            return reply; // caller cancelled; Pipe closes curl
                        }
                    }
                }
            }

            const auto done = event.find("done");
            finished        = done != event.end() && done->is_boolean() && done->get<bool>();
        }

        const int exit_code = pipe.close();
        if (!finished) {
            if (exit_code != 0) {
                throw std::runtime_error("request to Ollama failed (curl exit code " +
                                         std::to_string(exit_code) + "): " + diagnostics);
            }
            throw std::runtime_error("Ollama stream ended unexpectedly: " + diagnostics);
        }

        return reply;
    }

    std::vector<std::string> OllamaClient::list_models() const {
        // -f: an HTTP error status fails curl instead of printing the error page.
        const std::string command = "curl -sSf --max-time 5 '" +
                                    shell_escape_single_quotes(base_url_ + "/api/tags") + "' 2>&1";

        Pipe        pipe(command);
        std::string output;
        std::string line;
        while (pipe.read_line(line)) {
            output += line + "\n";
        }

        const int exit_code = pipe.close();
        if (exit_code != 0) {
            throw std::runtime_error("curl exit code " + std::to_string(exit_code) + ": " + output);
        }

        const json response = json::parse(output, nullptr, /*allow_exceptions=*/false);
        if (response.is_discarded() || !response.is_object()) {
            throw std::runtime_error("unexpected answer from /api/tags: " + output);
        }

        std::vector<std::string> names;
        if (const auto models = response.find("models");
            models != response.end() && models->is_array()) {
            for (const auto& model : *models) {
                if (model.is_object() && model.contains("name") && model["name"].is_string()) {
                    names.push_back(model["name"].get<std::string>());
                }
            }
        }
        return names;
    }

} // namespace yuka
