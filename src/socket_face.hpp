#pragma once

#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <sys/types.h>

#include "face.hpp"
#include "face_discovery.hpp"

namespace yuka {

    // Socket used in attach (development) mode: YUKA_FACE=attach.
    inline constexpr const char* k_dev_socket_path = "/tmp/yuka-dev.sock";

    // A GUI face connected over a Unix domain socket using NDJSON (one JSON object per line).
    //   agent → face: hello, assistant_start, assistant_chunk, assistant_end, error, bye
    //   face → agent: user_message
    class SocketFace final : public Face {
    public:
        // Starts the face process (passing $YUKA_SOCKET) and waits until it connects.
        // Returns nullptr if interrupted by Ctrl+C. Throws std::runtime_error on failure.
        static std::unique_ptr<SocketFace> launch(const FaceManifest&  manifest,
                                                  std::chrono::seconds timeout);

        // Listens on `socket_path` and waits for an externally started face (dev mode).
        // Returns nullptr if interrupted by Ctrl+C.
        static std::unique_ptr<SocketFace> attach(const std::string& socket_path);

        ~SocketFace() override;

        SocketFace(const SocketFace&)            = delete;
        SocketFace& operator=(const SocketFace&) = delete;

        void start_session(const AgentInfo& agent, const std::string& greeting) override;
        std::optional<std::string> read_user_message() override;
        void                       begin_reply() override;
        void                       reply_chunk(const std::string& text) override;
        void                       end_reply() override;
        void                       show_error(const std::string& message) override;
        bool                       is_alive() const override { return alive_; }

    private:
        SocketFace(std::string socket_path, int listen_fd);

        // Waits for the face to connect. Returns false if interrupted by Ctrl+C.
        bool wait_for_connection(std::optional<std::chrono::steady_clock::time_point> deadline);
        void send(const nlohmann::json& message);
        void shutdown_child();

        std::string socket_path_;
        int         listen_fd_ = -1;
        int         conn_fd_   = -1;
        pid_t       child_pid_ = -1;
        std::string child_log_;
        std::string read_buffer_;
        int         reply_id_   = 0;
        bool        reply_open_ = false;
        bool        alive_      = false;
    };

} // namespace yuka
