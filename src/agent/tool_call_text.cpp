#include "agent/tool_call_text.hpp"

namespace yuka {

    namespace {

        using nlohmann::json;

        std::string trim(const std::string& text) {
            const auto begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                return {};
            }
            const auto end = text.find_last_not_of(" \t\r\n");
            return text.substr(begin, end - begin + 1);
        }

        bool starts_with(const std::string& text, const std::string& prefix) {
            return text.compare(0, prefix.size(), prefix) == 0;
        }

        bool ends_with(const std::string& text, const std::string& suffix) {
            return text.size() >= suffix.size() &&
                   text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        // Removes one wrapper: <tool_call>…</tool_call> or a ``` fence (with optional language).
        std::string unwrap(std::string text) {
            text = trim(text);
            if (starts_with(text, "<tool_call>")) {
                text = text.substr(11);
                if (ends_with(text, "</tool_call>")) {
                    text.resize(text.size() - 12);
                }
                return trim(text);
            }
            if (starts_with(text, "```")) {
                const auto newline = text.find('\n');
                text               = newline == std::string::npos ? "" : text.substr(newline + 1);
                if (ends_with(trim(text), "```")) {
                    text = trim(text);
                    text.resize(text.size() - 3);
                }
                return trim(text);
            }
            return text;
        }

        std::optional<ToolCall> to_call(const json& value,
                                        const std::function<bool(const std::string&)>& exists) {
            if (!value.is_object()) {
                return std::nullopt;
            }
            // {"name": …, "arguments": {…}} or OpenAI-style {"function": {"name", "arguments"}}
            const json& body = value.contains("function") && value["function"].is_object()
                                   ? value["function"]
                                   : value;
            const auto name = body.find("name");
            if (name == body.end() || !name->is_string() || !exists(name->get<std::string>())) {
                return std::nullopt;
            }
            json arguments = json::object();
            for (const char* key : {"arguments", "parameters"}) {
                if (const auto it = body.find(key); it != body.end()) {
                    arguments = it->is_string() ? json::parse(it->get<std::string>(), nullptr, false)
                                                : *it;
                    break;
                }
            }
            if (!arguments.is_object()) {
                return std::nullopt;
            }
            return ToolCall{{}, name->get<std::string>(), std::move(arguments)};
        }

    } // namespace

    std::optional<std::vector<ToolCall>>
    parse_textual_tool_calls(const std::string&                             text,
                             const std::function<bool(const std::string&)>& tool_exists) {
        // Several <tool_call> blocks in a row are joined into one array.
        std::string body = trim(text);
        if (starts_with(body, "<tool_call>")) {
            std::string joined = "[";
            std::size_t pos    = 0;
            while ((pos = body.find("<tool_call>", pos)) != std::string::npos) {
                const auto end = body.find("</tool_call>", pos);
                const auto block =
                    body.substr(pos + 11, (end == std::string::npos ? body.size() : end) - pos - 11);
                joined += (joined.size() > 1 ? "," : "") + trim(block);
                if (end == std::string::npos) {
                    break;
                }
                pos = end + 12;
            }
            body = joined + "]";
        } else {
            body = unwrap(body);
        }

        const json parsed = json::parse(body, nullptr, /*allow_exceptions=*/false);
        if (parsed.is_discarded()) {
            return std::nullopt;
        }

        std::vector<ToolCall> calls;
        if (parsed.is_array()) {
            for (const auto& item : parsed) {
                auto call = to_call(item, tool_exists);
                if (!call) {
                    return std::nullopt; // all or nothing
                }
                calls.push_back(std::move(*call));
            }
        } else if (auto call = to_call(parsed, tool_exists)) {
            calls.push_back(std::move(*call));
        }
        if (calls.empty()) {
            return std::nullopt;
        }
        return calls;
    }

    bool may_be_textual_tool_call(const std::string& text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return false;
        }
        const char c = text[first];
        return c == '{' || c == '[' || c == '<' || c == '`';
    }

} // namespace yuka
