#include "faces/socket/socket_face.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "platform/signals.hpp"
#include "platform/terminal.hpp"

extern char** environ;

namespace yuka {

    namespace {

        using nlohmann::json;
        using Clock = std::chrono::steady_clock;

        constexpr int k_poll_interval_ms = 200;

        // The face protocol version this agent speaks (docs/face-protocol.md).
        constexpr int k_protocol_version = 2;

        // ^[a-z][a-z0-9_]{0,63}$
        bool is_valid_tool_name(const std::string& name) {
            if (name.empty() || name.size() > 64 || name[0] < 'a' || name[0] > 'z') {
                return false;
            }
            for (char c : name) {
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
                    return false;
                }
            }
            return true;
        }

        std::string errno_message(const char* what) {
            return std::string(what) + ": " + std::strerror(errno);
        }

        void set_cloexec(int fd) {
            fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
        }

        // Creates a listening Unix socket accessible only by the current user.
        int create_listener(const std::string& path) {
            sockaddr_un address{};
            if (path.size() >= sizeof(address.sun_path)) {
                throw std::runtime_error("socket path is too long: " + path);
            }
            address.sun_family = AF_UNIX;
            std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

            const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd == -1) {
                throw std::runtime_error(errno_message("socket"));
            }
            set_cloexec(fd);

            unlink(path.c_str());
            const mode_t previous_mask = umask(0077);
            const int    bound = bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
            umask(previous_mask);

            if (bound == -1 || listen(fd, 1) == -1) {
                const std::string message = errno_message(bound == -1 ? "bind" : "listen");
                close(fd);
                unlink(path.c_str());
                throw std::runtime_error(message + " (" + path + ")");
            }
            return fd;
        }

        // Current environment with $YUKA_SOCKET set to `socket_path`.
        std::vector<std::string> child_environment(const std::string& socket_path) {
            std::vector<std::string> env;
            for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
                if (std::strncmp(*entry, "YUKA_SOCKET=", 12) != 0) {
                    env.emplace_back(*entry);
                }
            }
            env.push_back("YUKA_SOCKET=" + socket_path);
            return env;
        }

        std::vector<char*> to_argv(std::vector<std::string>& strings) {
            std::vector<char*> result;
            result.reserve(strings.size() + 1);
            for (auto& s : strings) {
                result.push_back(s.data());
            }
            result.push_back(nullptr);
            return result;
        }

        pid_t spawn_face(const FaceManifest& manifest,
                         const std::string&  socket_path,
                         const std::string&  log_path) {
            std::vector<std::string> args{manifest.executable.string()};
            args.insert(args.end(), manifest.args.begin(), manifest.args.end());
            std::vector<std::string> env = child_environment(socket_path);

            std::vector<char*> argv = to_argv(args);
            std::vector<char*> envp = to_argv(env);

            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
            posix_spawn_file_actions_addopen(
                &actions, STDOUT_FILENO, log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

            posix_spawnattr_t attributes;
            posix_spawnattr_init(&attributes);
            // Own process group: Ctrl+C in the terminal goes to yuka, which then shuts the
            // face down in order. CLOEXEC_DEFAULT: the child inherits only stdio.
            posix_spawnattr_setflags(&attributes,
                                     POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_CLOEXEC_DEFAULT);
            posix_spawnattr_setpgroup(&attributes, 0);

            pid_t     pid = -1;
            const int rc =
                posix_spawn(&pid, argv[0], &actions, &attributes, argv.data(), envp.data());

            posix_spawn_file_actions_destroy(&actions);
            posix_spawnattr_destroy(&attributes);

            if (rc != 0) {
                throw std::runtime_error("failed to start " + manifest.executable.string() + ": " +
                                         std::strerror(rc));
            }
            return pid;
        }

    } // namespace

    SocketFace::SocketFace(std::string socket_path, int listen_fd)
        : socket_path_(std::move(socket_path)), listen_fd_(listen_fd) {}

    std::unique_ptr<SocketFace> SocketFace::launch(const FaceManifest&  manifest,
                                                   std::chrono::seconds timeout) {
        const std::string socket_path = "/tmp/yuka-" + std::to_string(getpid()) + ".sock";

        std::unique_ptr<SocketFace> face(new SocketFace(socket_path, create_listener(socket_path)));
        face->child_log_ = "/tmp/yuka-face-" + manifest.name + ".log";
        face->child_pid_ = spawn_face(manifest, socket_path, face->child_log_);

        if (!face->wait_for_connection(Clock::now() + timeout)) {
            return nullptr;
        }
        return face;
    }

    std::unique_ptr<SocketFace> SocketFace::attach(const std::string& socket_path) {
        std::unique_ptr<SocketFace> face(new SocketFace(socket_path, create_listener(socket_path)));
        if (!face->wait_for_connection(std::nullopt)) {
            return nullptr;
        }
        return face;
    }

    SocketFace::~SocketFace() {
        if (alive_) {
            send({{"type", "bye"}});
        }
        if (conn_fd_ != -1) {
            ::shutdown(conn_fd_, SHUT_RDWR);
            close(conn_fd_);
        }
        if (listen_fd_ != -1) {
            close(listen_fd_);
            unlink(socket_path_.c_str());
        }
        shutdown_child();
    }

    bool SocketFace::wait_for_connection(std::optional<Clock::time_point> deadline) {
        while (true) {
            if (stop_requested()) {
                return false;
            }

            if (child_pid_ > 0) {
                int status = 0;
                if (waitpid(child_pid_, &status, WNOHANG) == child_pid_) {
                    child_pid_ = -1;
                    throw std::runtime_error("the face process exited before connecting (see " +
                                             child_log_ + ")");
                }
            }

            if (deadline && Clock::now() >= *deadline) {
                throw std::runtime_error("the face did not connect in time (see " + child_log_ +
                                         ")");
            }

            pollfd    pfd{listen_fd_, POLLIN, 0};
            const int ready = poll(&pfd, 1, k_poll_interval_ms);
            if (ready <= 0) {
                continue; // timeout or EINTR – re-check conditions
            }

            const int fd = accept(listen_fd_, nullptr, nullptr);
            if (fd == -1) {
                continue;
            }
            set_cloexec(fd);
            const int on = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));

            conn_fd_ = fd;
            alive_   = true;

            // One face per session: stop listening and remove the socket file.
            close(listen_fd_);
            listen_fd_ = -1;
            unlink(socket_path_.c_str());
            return true;
        }
    }

    void SocketFace::send(const json& message) {
        if (!alive_) {
            return;
        }
        // Replace invalid UTF-8 instead of throwing: model output must never crash the agent.
        const std::string line =
            message.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";

        const char* data      = line.data();
        std::size_t remaining = line.size();
        while (remaining > 0) {
            const ssize_t written = write(conn_fd_, data, remaining);
            if (written < 0 && errno == EINTR) {
                continue;
            }
            if (written <= 0) {
                alive_ = false;
                return;
            }
            data += written;
            remaining -= static_cast<std::size_t>(written);
        }
    }

    void SocketFace::start_session(const AgentInfo& agent, const std::string& greeting) {
        send({{"type", "hello"},
              {"protocol", k_protocol_version},
              {"agent", {{"name", agent.name}, {"model", agent.model}}},
              {"greeting", greeting}});
    }

    bool SocketFace::pump(int timeout_ms) {
        if (!alive_) {
            return false;
        }

        // Read only when no complete line is buffered yet.
        if (read_buffer_.find('\n') == std::string::npos) {
            pollfd    pfd{conn_fd_, POLLIN, 0};
            const int ready = poll(&pfd, 1, timeout_ms);
            if (ready > 0) {
                std::array<char, 4096> buffer{};
                const ssize_t          n = read(conn_fd_, buffer.data(), buffer.size());
                if (n > 0) {
                    read_buffer_.append(buffer.data(), static_cast<std::size_t>(n));
                } else if (n == 0 || errno != EINTR) {
                    alive_ = false; // window closed
                }
            }
            // ready <= 0: timeout or EINTR – the caller re-checks its conditions.
        }

        std::size_t newline = 0;
        while ((newline = read_buffer_.find('\n')) != std::string::npos) {
            const std::string line = read_buffer_.substr(0, newline);
            read_buffer_.erase(0, newline + 1);
            handle_line(line);
        }
        return alive_;
    }

    void SocketFace::handle_line(const std::string& line) {
        const json message = json::parse(line, nullptr, /*allow_exceptions=*/false);
        if (!message.is_object()) {
            return; // malformed – ignore
        }
        const auto type_it = message.find("type");
        if (type_it == message.end() || !type_it->is_string()) {
            return;
        }
        const std::string type = type_it->get<std::string>();

        if (type == "user_message") {
            const auto text = message.find("text");
            if (text != message.end() && text->is_string()) {
                pending_messages_.push_back(text->get<std::string>());
            }
        } else if (type == "capabilities") {
            apply_capabilities(message);
        } else if (type == "tool_result") {
            const auto id = message.find("id");
            if (id == message.end() || !id->is_string() || awaited_call_.empty() ||
                id->get<std::string>() != awaited_call_) {
                return; // late (after a timeout) or unknown – ignore
            }
            std::string text;
            if (const auto content = message.find("content");
                content != message.end() && content->is_array()) {
                for (const auto& part : *content) {
                    if (part.is_object() && part.value("type", "") == "text" &&
                        part.contains("text") && part["text"].is_string()) {
                        text += (text.empty() ? "" : "\n") + part["text"].get<std::string>();
                    }
                }
            }
            const auto is_error = message.find("isError");
            awaited_result_ =
                ToolResult{text, is_error != message.end() && is_error->is_boolean() &&
                                     is_error->get<bool>()};
        }
        // Anything else: a newer face – ignore.
    }

    void SocketFace::apply_capabilities(const json& message) {
        std::vector<FaceTool> tools;
        if (const auto list = message.find("tools"); list != message.end() && list->is_array()) {
            for (const auto& entry : *list) {
                const std::string name =
                    entry.is_object() ? entry.value("name", std::string()) : std::string();
                const auto reject = [&](const std::string& why) {
                    term::print_error("инструмент интерфейса «" + name + "» пропущен: " + why +
                                      ".");
                };
                if (!is_valid_tool_name(name)) {
                    reject("имя должно быть вида ui_set_theme");
                    continue;
                }
                const auto description = entry.find("description");
                if (description == entry.end() || !description->is_string() ||
                    description->get<std::string>().empty()) {
                    reject("нет описания");
                    continue;
                }
                const auto schema = entry.find("inputSchema");
                if (schema == entry.end() || !schema->is_object() ||
                    schema->value("type", "") != "object") {
                    reject("inputSchema должна быть объектом с type: object");
                    continue;
                }
                bool duplicate = false;
                for (const auto& tool : tools) {
                    duplicate = duplicate || tool.spec.name == name;
                }
                if (duplicate) {
                    reject("имя повторяется");
                    continue;
                }

                FaceTool tool;
                tool.spec  = {name, description->get<std::string>(), *schema};
                tool.title = entry.contains("title") && entry["title"].is_string()
                                 ? entry["title"].get<std::string>()
                                 : name;
                if (const auto notes = entry.find("annotations");
                    notes != entry.end() && notes->is_object()) {
                    const auto flag = [&](const char* key) {
                        const auto it = notes->find(key);
                        return it != notes->end() && it->is_boolean() && it->get<bool>();
                    };
                    tool.annotations = {flag("readOnlyHint"),
                                        flag("destructiveHint"),
                                        flag("idempotentHint")};
                }
                tools.push_back(std::move(tool));
            }
        }

        tools_ = std::move(tools);
        const auto instructions = message.find("instructions");
        instructions_ = instructions != message.end() && instructions->is_string()
                            ? instructions->get<std::string>()
                            : std::string();
        tools_changed_ = true;
    }

    bool SocketFace::take_tools_changed() {
        // Apply whatever the face has already sent (capabilities usually follow hello).
        pump(0);
        return std::exchange(tools_changed_, false);
    }

    std::optional<std::string> SocketFace::read_user_message() {
        while (true) {
            if (!pending_messages_.empty()) {
                std::string text = std::move(pending_messages_.front());
                pending_messages_.pop_front();
                return text;
            }
            if (!alive_ || stop_requested()) {
                return std::nullopt;
            }
            pump(k_poll_interval_ms);
        }
    }

    ToolResult SocketFace::call_tool(const std::string&        name,
                                     const json&               arguments,
                                     std::chrono::milliseconds timeout) {
        if (!alive_) {
            return ToolResult::error("Интерфейс закрыт.");
        }

        awaited_call_ = "call_" + std::to_string(++next_call_id_);
        awaited_result_.reset();
        send({{"type", "tool_call"},
              {"id", awaited_call_},
              {"name", name},
              {"arguments", arguments}});

        const auto deadline = Clock::now() + timeout;
        ToolResult result;
        while (true) {
            if (awaited_result_) {
                result = std::move(*awaited_result_);
                break;
            }
            if (!alive_) {
                result = ToolResult::error("Интерфейс закрыт, действие не выполнено.");
                break;
            }
            if (stop_requested()) {
                result = ToolResult::error("Прервано пользователем.");
                break;
            }
            if (Clock::now() >= deadline) {
                result = ToolResult::error("Интерфейс не ответил вовремя, действие могло не "
                                           "выполниться.");
                break;
            }
            pump(k_poll_interval_ms);
        }
        awaited_call_.clear();
        awaited_result_.reset();
        return result;
    }

    void SocketFace::begin_reply() {
        ++reply_id_;
        reply_open_ = true;
        send({{"type", "assistant_start"}, {"id", reply_id_}});
    }

    void SocketFace::reply_chunk(const std::string& text) {
        send({{"type", "assistant_chunk"}, {"id", reply_id_}, {"text", text}});
    }

    void SocketFace::end_reply() {
        if (!reply_open_) {
            return;
        }
        reply_open_ = false;
        send({{"type", "assistant_end"}, {"id", reply_id_}});
    }

    void SocketFace::show_error(const std::string& message) {
        end_reply();
        send({{"type", "error"}, {"message", message}});
    }

    void SocketFace::shutdown_child() {
        if (child_pid_ <= 0) {
            return;
        }

        // The face quits by itself after "bye"/EOF; give it a moment, then insist.
        const auto wait_exit = [this](std::chrono::milliseconds limit) {
            const auto until = Clock::now() + limit;
            while (Clock::now() < until) {
                int status = 0;
                if (waitpid(child_pid_, &status, WNOHANG) == child_pid_) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return false;
        };

        if (!wait_exit(std::chrono::milliseconds(3000))) {
            kill(-child_pid_, SIGTERM); // whole process group (Electron helpers)
            if (!wait_exit(std::chrono::milliseconds(2000))) {
                kill(-child_pid_, SIGKILL);
                waitpid(child_pid_, nullptr, 0);
            }
        }
        child_pid_ = -1;
    }

} // namespace yuka
