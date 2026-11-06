#include "agent/chat_session.hpp"

#include <exception>

#include "agent/tool_call_text.hpp"
#include "platform/signals.hpp"
#include "platform/terminal.hpp"
#include "tools/builtin/app_tools.hpp"
#include "tools/builtin/link_tools.hpp"
#include "tools/builtin/load_skill_tool.hpp"
#include "tools/builtin/project_tools.hpp"
#include "tools/remote_tool.hpp"

namespace yuka {

    namespace {

        // Streams model text to the face, except text that may be a tool call written as
        // text: that is held back until the reply is complete and then either executed as a
        // call or shown after all. Between the visible parts of several model calls of one
        // turn a paragraph break is inserted.
        class StreamGate {
        public:
            // `may_hold`: tools exist, so a written tool call is possible. Without tools every
            // chunk is streamed immediately, exactly like a plain chat.
            StreamGate(Face& face, bool shown_before, bool may_hold)
                : face_(face), separate_(shown_before), decided_(!may_hold) {}

            bool on_chunk(const std::string& chunk) {
                if (holding_) {
                    held_ += chunk;
                } else if (!decided_) {
                    held_ += chunk;
                    if (held_.find_first_not_of(" \t\r\n") != std::string::npos) {
                        decided_ = true;
                        holding_ = may_be_textual_tool_call(held_);
                        if (!holding_) {
                            show(std::exchange(held_, {}));
                        }
                    }
                } else {
                    show(chunk);
                }
                return !stop_requested() && face_.is_alive();
            }

            // The held text (empty if everything was streamed).
            const std::string& held() const { return held_; }

            // Shows what was held back (it was not a tool call after all).
            void release() { show(std::exchange(held_, {})); }

            bool shown_anything() const { return shown_; }

        private:
            void show(const std::string& text) {
                if (text.empty()) {
                    return;
                }
                if (separate_ && !shown_) {
                    face_.reply_chunk("\n\n");
                }
                shown_ = true;
                face_.reply_chunk(text);
            }

            Face&       face_;
            bool        separate_;
            bool        decided_;
            bool        holding_ = false;
            bool        shown_   = false;
            std::string held_;
        };

        std::string shorten(std::string text, std::size_t limit) {
            if (text.size() <= limit) {
                return text;
            }
            std::size_t cut = limit;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
                --cut;
            }
            text.resize(cut);
            return text + "…";
        }

    } // namespace

    ChatSession::ChatSession(Face&                 face,
                             const SessionContext& session,
                             const OllamaClient&   client,
                             const AgentConfig&    config,
                             SkillLibrary          skills,
                             AccessConfig          access,
                             bool                  report_tools_in_terminal)
        : face_(face),
          session_(session),
          client_(client),
          config_(config),
          skills_(std::move(skills)),
          access_(std::move(access)),
          report_tools_(report_tools_in_terminal) {
        std::vector<std::unique_ptr<Tool>> builtin;
        if (!skills_.empty()) {
            builtin.push_back(make_load_skill_tool(skills_));
        }
        // What config/access.json allows: programs, project folders and web pages.
        for (auto& tool : make_app_tools(access_)) {
            builtin.push_back(std::move(tool));
        }
        for (auto& tool : make_project_tools(access_)) {
            builtin.push_back(std::move(tool));
        }
        for (auto& tool : make_link_tools(access_)) {
            builtin.push_back(std::move(tool));
        }
        tools_.replace_source(ToolSource::builtin, std::move(builtin));
    }

    void ChatSession::run() {
        // The greeting depends on how the user reaches Yuka in this session.
        greeting_ = compose_greeting(config_, session_);

        history_.clear();
        history_.push_back(ChatMessage::system({}));
        history_.push_back(ChatMessage::assistant(greeting_));
        refresh_tools();

        face_.start_session({config_.name, config_.model}, greeting_);

        while (!stop_requested() && face_.is_alive()) {
            const auto input = face_.read_user_message();
            if (!input) {
                break;
            }
            if (input->empty()) {
                continue;
            }
            run_turn(*input);
        }
    }

    // Re-reads the face's tools and rebuilds the system prompt (history_[0]) around them.
    void ChatSession::refresh_tools() {
        tools_.replace_source(ToolSource::face, remote_tools(face_));

        PromptCapabilities capabilities;
        capabilities.has_face_tools    = !face_.tools().empty();
        capabilities.face_instructions = face_.tool_instructions();
        for (const auto& skill : skills_.skills()) {
            capabilities.skills.emplace_back(skill.name, skill.description);
        }
        history_[0].content = build_system_prompt(config_, session_, greeting_, capabilities);
    }

    void ChatSession::run_turn(const std::string& input) {
        if (face_.take_tools_changed()) {
            refresh_tools();
        }
        tools_.refresh(); // e.g. the current list of projects on disk

        const std::size_t checkpoint = history_.size(); // rollback point
        const auto        rollback   = [&] { history_.resize(checkpoint); };

        history_.push_back(ChatMessage::user(input));
        face_.begin_reply();

        bool shown = false; // visible text already streamed in this turn
        try {
            for (std::size_t step = 0; step < k_max_model_calls; ++step) {
                StreamGate gate(face_, shown, !tools_.empty());
                ChatReply  reply = client_.chat(
                    history_, tools_.specs(), [&](const std::string& chunk) {
                        return gate.on_chunk(chunk);
                    });

                if (stop_requested() || !face_.is_alive()) {
                    face_.end_reply();
                    rollback(); // interrupted: the reply is not kept
                    return;
                }

                // A tool call written as text: run it instead of showing it.
                if (reply.tool_calls.empty() && !gate.held().empty() && !tools_.empty()) {
                    if (auto calls = parse_textual_tool_calls(
                            gate.held(),
                            [&](const std::string& name) { return tools_.find(name) != nullptr; })) {
                        reply.tool_calls = std::move(*calls);
                        reply.text.clear();
                    }
                }

                if (reply.tool_calls.empty()) {
                    gate.release();
                    face_.end_reply();
                    history_.push_back(ChatMessage::assistant(reply.text));
                    trim_history();
                    return;
                }

                // The model wants tools: run them and give it the results.
                shown = shown || gate.shown_anything();
                for (auto& call : reply.tool_calls) {
                    if (call.id.empty()) {
                        call.id = "call_" + std::to_string(++next_call_id_);
                    }
                }
                history_.push_back(ChatMessage::assistant(reply.text, reply.tool_calls));

                for (const auto& call : reply.tool_calls) {
                    report_tool(tools_.find(call.name), call);
                    const ToolResult result = tools_.call(call.name, call.arguments);
                    history_.push_back(ChatMessage::tool(
                        call.name, result.is_error ? "Ошибка: " + result.text : result.text));

                    if (stop_requested() || !face_.is_alive()) {
                        face_.end_reply();
                        rollback();
                        return;
                    }
                }

                // A face may update its tools while a call runs (e.g. new choices).
                if (face_.take_tools_changed()) {
                    refresh_tools();
                }
            }

            rollback();
            face_.show_error("Юка сделала слишком много шагов за один ответ и остановилась. "
                             "Попробуй сформулировать просьбу проще.");
        } catch (const std::exception& ex) {
            rollback(); // drop the unanswered user message
            if (stop_requested()) {
                return;
            }
            face_.show_error(ex.what());
        }
    }

    // Keeps the system prompt (index 0) and the most recent messages. Tool results are never
    // left without the assistant message that asked for them.
    void ChatSession::trim_history() {
        if (history_.size() <= k_max_history_messages + 1) {
            return;
        }
        auto first = history_.begin() + 1 +
                     static_cast<long>(history_.size() - (k_max_history_messages + 1));
        while (first != history_.end() && first->role == "tool") {
            ++first;
        }
        history_.erase(history_.begin() + 1, first);
    }

    // GUI sessions: the terminal shows what Yuka does ("Юка → Сменить тему {"mode":"night"}").
    // Arguments are shortened; the user's own text is never printed.
    void ChatSession::report_tool(const Tool* tool, const ToolCall& call) const {
        if (!report_tools_) {
            return;
        }
        const std::string title = tool != nullptr ? tool->title() : call.name;
        term::print_info(config_.name + " → " + title + " " +
                         shorten(call.arguments.dump(-1, ' ', false,
                                                     nlohmann::json::error_handler_t::replace),
                                 120));
    }

} // namespace yuka
