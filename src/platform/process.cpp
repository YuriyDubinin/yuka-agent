#include "platform/process.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace yuka {

    CommandResult run_command(const std::vector<std::string>& argv,
                              std::chrono::milliseconds       timeout) {
        CommandResult result;
        if (argv.empty()) {
            return result;
        }

        int pipe_fds[2];
        if (pipe(pipe_fds) != 0) {
            return result;
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
        posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);

        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (const auto& arg : argv) {
            args.push_back(const_cast<char*>(arg.c_str()));
        }
        args.push_back(nullptr);

        pid_t     pid = -1;
        const int rc  = posix_spawn(&pid, args[0], &actions, nullptr, args.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        close(pipe_fds[1]);

        if (rc != 0) {
            close(pipe_fds[0]);
            result.output = "не удалось запустить " + argv[0];
            return result;
        }

        // Read until EOF or the deadline.
        const auto             deadline = std::chrono::steady_clock::now() + timeout;
        std::array<char, 4096> buffer{};
        while (true) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0) {
                result.timed_out = true;
                kill(pid, SIGKILL);
                break;
            }
            pollfd    pfd{pipe_fds[0], POLLIN, 0};
            const int ready = poll(&pfd, 1, static_cast<int>(left.count()));
            if (ready < 0 && errno == EINTR) {
                continue;
            }
            if (ready <= 0) {
                continue; // re-check the deadline
            }
            const ssize_t n = read(pipe_fds[0], buffer.data(), buffer.size());
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                break; // EOF: the program closed its output
            }
            result.output.append(buffer.data(), static_cast<std::size_t>(n));
        }
        close(pipe_fds[0]);

        int status = 0;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }
        if (!result.timed_out && WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        }
        return result;
    }

} // namespace yuka
