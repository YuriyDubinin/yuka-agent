#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

    volatile std::sig_atomic_t g_stop_requested = 0;

    constexpr const char* k_color_reset = "\033[0m";

    constexpr const char* k_color_banner_title = "\033[1;38;5;123m";
    constexpr const char* k_color_banner_body  = "\033[1;38;5;141m";

    constexpr const char* k_color_user_prompt = "\033[38;5;81m";
    constexpr const char* k_color_ai_prompt   = "\033[38;5;141m";

    constexpr const char* k_color_ai_text = "\033[38;5;252m";

    constexpr const char* k_color_info = "\033[38;5;108m";

    constexpr const char* k_color_error = "\033[38;5;174m";

    void handle_sig_int(int) {
        g_stop_requested = 1;
    }

    std::string shell_escape_single_quotes(const std::string& input) {
        std::string escaped;
        escaped.reserve(input.size() + 16);

        for (char c : input) {
            if (c == '\'') {
                escaped += "'\\''";
            } else {
                escaped += c;
            }
        }

        return escaped;
    }

    bool is_ollama_available() {
        return std::system("command -v ollama >/dev/null 2>&1") == 0;
    }

    bool is_ollama_serve_running() {
        return std::system("ollama list >/dev/null 2>&1") == 0;
    }

    bool start_ollama_serve_if_needed() {
        if (is_ollama_serve_running()) {
            return true;
        }

        std::cout << k_color_info << "Ollama service is not running. Starting 'ollama serve'..."
                  << k_color_reset << "\n";

        const int start_rc = std::system("nohup ollama serve >/tmp/yuka-ollama-serve.log 2>&1 &");
        if (start_rc != 0) {
            std::cerr << k_color_error << " Error: Failed to start 'ollama serve'. "
                      << k_color_reset << "\n";
            return false;
        }

        // Wait briefly until Ollama API becomes responsive.
        for (int i = 0; i < 40; ++i) {
            if (g_stop_requested) {
                return false;
            }
            if (is_ollama_serve_running()) {
                std::cout << k_color_info << "Ollama service started." << k_color_reset << "\n";
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        std::cerr << k_color_error << " Error: 'ollama serve' did not become ready in time. "
                  << k_color_reset << "\n";
        return false;
    }

    bool ensure_system_terminal(int argc, char* argv[]) {
        if (std::getenv("YUKA_IN_SYSTEM_TERMINAL") != nullptr) {
            return true;
        }

        const char* term_program = std::getenv("TERM_PROGRAM");
        if (term_program != nullptr && std::string(term_program) == "Apple_Terminal") {
            return true;
        }

        if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
            return true;
        }

        std::error_code ec;
        const auto      executable_path = std::filesystem::absolute(argv[0], ec);
        if (ec) {
            return true;
        }

        const std::string escaped_exec = shell_escape_single_quotes(executable_path.string());
        const std::string terminal_cmd =
            "osascript -e 'tell application \"Terminal\" to do script \""
            "export YUKA_IN_SYSTEM_TERMINAL=1; "
            "" +
            escaped_exec + "\"' -e 'tell application \"Terminal\" to activate'";

        if (std::system(terminal_cmd.c_str()) != 0) {
            std::cerr << k_color_error << " Error: Failed to open system Terminal. "
                      << k_color_reset << "\n";
            return true;
        }

        return false;
    }

    void print_with_fast_typing_effect(const std::string& text) {
        // Fast "typewriter" effect: small delay for readability without feeling slow.
        constexpr auto k_delay = std::chrono::milliseconds(2);

        for (unsigned char ch : text) {
            if (g_stop_requested) {
                break;
            }
            std::cout.put(static_cast<char>(ch));
            std::cout.flush();
            std::this_thread::sleep_for(k_delay);
        }
    }

    void run_ollama_and_print(const std::string& prompt, int& exit_code) {
        const std::string escaped_prompt = shell_escape_single_quotes(prompt);
        const std::string command        = "ollama run qwen2.5:7b '" + escaped_prompt + "' 2>&1";

        std::array<char, 4096> buffer{};
        std::string            output;

        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            throw std::runtime_error("Failed to start Ollama process.");
        }

        while (!g_stop_requested &&
               std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        const int status = pclose(pipe);
        if (status == -1) {
            exit_code = -1;
        } else {
            exit_code = WEXITSTATUS(status);
        }

        std::cout << k_color_ai_prompt << " Yuka: > " << k_color_reset << " ";
        if (output.empty()) {
            std::cout << k_color_info << "(empty response)" << k_color_reset << "\n";
            return;
        }

        std::cout << k_color_ai_text;
        print_with_fast_typing_effect(output);
        std::cout << k_color_reset;
        if (!output.empty() && output.back() != '\n') {
            std::cout << '\n';
        }
    }

} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handle_sig_int);

    if (!is_ollama_available()) {
        std::cerr << k_color_error
                  << " Error: Ollama CLI not found. Please install Ollama and ensure 'ollama' is "
                     "in PATH. "
                  << k_color_reset << "\n";
        return 1;
    }

    if (!start_ollama_serve_if_needed()) {
        return 1;
    }

    if (!ensure_system_terminal(argc, argv)) {
        return 0;
    }

    std::cout << k_color_banner_title << " Yuka AI terminal started (qwen2.5:7b via Ollama) "
              << k_color_reset << "\n"
              << k_color_banner_body << "Type your message and press Enter. Type 'exit' to quit."
              << k_color_reset << "\n\n";

    std::string input;
    while (!g_stop_requested) {
        std::cout << k_color_user_prompt << " me: > " << k_color_reset << " " << std::flush;

        if (!std::getline(std::cin, input)) {
            if (g_stop_requested) {
                break;
            }
            std::cout << "\n";
            break;
        }

        if (input == "exit" || input == "quit") {
            break;
        }

        if (input.empty()) {
            continue;
        }

        try {
            int exit_code = 0;
            run_ollama_and_print(input, exit_code);

            if (exit_code != 0 && !g_stop_requested) {
                std::cerr << k_color_error << " Error: Ollama command failed with exit code "
                          << exit_code << ". " << k_color_reset << "\n";
            }
        } catch (const std::exception& ex) {
            std::cerr << k_color_error << " Error: " << ex.what() << " " << k_color_reset << "\n";
            return 1;
        }
    }

    std::cout << "\n" << k_color_info << "Bye!" << k_color_reset << "\n";
    return 0;
}