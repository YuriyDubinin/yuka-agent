#pragma once

#include "face.hpp"

namespace yuka {

    // Classic chat in the terminal (stdin/stdout) with a typewriter effect.
    class TerminalFace final : public Face {
    public:
        void start_session(const AgentInfo& agent, const std::string& greeting) override;
        std::optional<std::string> read_user_message() override;
        void                       begin_reply() override;
        void                       reply_chunk(const std::string& text) override;
        void                       end_reply() override;
        void                       show_error(const std::string& message) override;
        bool                       is_alive() const override { return true; }

    private:
        std::string agent_name_;
        bool        reply_open_      = false;
        bool        reply_has_text_  = false;
        bool        ends_with_break_ = false;
    };

} // namespace yuka
