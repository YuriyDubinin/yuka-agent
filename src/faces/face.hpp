#pragma once

#include <optional>
#include <string>

namespace yuka {

    struct AgentInfo {
        std::string name;
        std::string model;
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
    };

} // namespace yuka
