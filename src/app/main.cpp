#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "agent/config.hpp"
#include "faces/face.hpp"
#include "faces/face_discovery.hpp"
#include "faces/socket/socket_face.hpp"
#include "faces/terminal/terminal_face.hpp"
#include "llm/ollama_client.hpp"
#include "platform/shell.hpp"
#include "platform/signals.hpp"
#include "platform/terminal.hpp"

namespace {

    constexpr const char* k_ollama_base_url = "http://127.0.0.1:11434";

    // Upper bound on user/assistant messages kept in context (the system prompt is always kept).
    constexpr std::size_t k_max_history_messages = 40;

    // How long a GUI face may take to start and connect.
    constexpr std::chrono::seconds k_face_connect_timeout{30};

    // "qwen2.5" in the config matches "qwen2.5:latest" on the server, like the Ollama CLI does.
    bool is_same_model(const std::string& installed, const std::string& wanted) {
        return installed == wanted ||
               (wanted.find(':') == std::string::npos && installed == wanted + ":latest");
    }

    // Connects to an already running Ollama server and checks that the configured model is
    // installed. Yuka never starts Ollama itself: if the server or the model is missing, she
    // reports it and the agent does not start.
    bool check_ollama(const yuka::OllamaClient& client) {
        std::vector<std::string> models;
        try {
            models = client.list_models();
        } catch (const std::exception& ex) {
            yuka::term::print_error(std::string("сервер Ollama недоступен по адресу ") +
                                    k_ollama_base_url + " (" + ex.what() + ").");
            yuka::term::print_info("Запусти Ollama (приложение или 'ollama serve') и повтори.");
            return false;
        }

        for (const auto& installed : models) {
            if (is_same_model(installed, client.model())) {
                return true;
            }
        }

        std::string installed_list;
        for (const auto& installed : models) {
            installed_list += (installed_list.empty() ? "" : ", ") + installed;
        }
        yuka::term::print_error(
            "модель '" + client.model() + "' не найдена на сервере Ollama (" +
            (installed_list.empty() ? std::string("моделей нет") : "есть: " + installed_list) +
            ").");
        yuka::term::print_info("Установи её: ollama pull " + client.model());
        return false;
    }

    // Set by the Terminal.app relaunch when the terminal is a fallback, not a choice.
    constexpr const char* k_env_terminal_fallback = "YUKA_TERMINAL_FALLBACK";

    // Relaunches the binary inside macOS Terminal.app unless already running there.
    // Returns true if execution should continue in the current process.
    bool ensure_system_terminal(const yuka::SessionContext& session) {
        if (std::getenv("YUKA_IN_SYSTEM_TERMINAL") != nullptr) {
            return true;
        }

        const char* term_program = std::getenv("TERM_PROGRAM");
        if (term_program != nullptr && std::string(term_program) == "Apple_Terminal") {
            return true;
        }

        // The real path of this binary, whatever it is called (build/yuka-agent by default).
        const std::filesystem::path executable_path = yuka::executable_path();
        if (executable_path.empty()) {
            return true;
        }

        // Command executed by the shell inside Terminal.app. Paths are wrapped in \" so that
        // AppleScript passes them to that shell as double-quoted arguments.
        // YUKA_FACE=terminal: the relaunched process must not try GUI faces again;
        // YUKA_TERMINAL_FALLBACK keeps the "no face worked" greeting.
        std::string inner_cmd = "export YUKA_IN_SYSTEM_TERMINAL=1 YUKA_FACE=terminal; ";
        if (session.interface == yuka::SessionInterface::terminal_backup) {
            inner_cmd += std::string("export ") + k_env_terminal_fallback + "=1; ";
        }
        if (const char* storage = std::getenv("YUKA_STORAGE"); storage != nullptr) {
            inner_cmd += "export YUKA_STORAGE=\\\"" + std::string(storage) + "\\\"; ";
        }
        inner_cmd += "\\\"" + executable_path.string() + "\\\"";

        const std::string terminal_cmd =
            "osascript -e 'tell application \"Terminal\" to do script \"" +
            yuka::shell_escape_single_quotes(inner_cmd) +
            "\"' -e 'tell application \"Terminal\" to activate'";

        if (std::system(terminal_cmd.c_str()) != 0) {
            yuka::term::print_error(
                "не удалось открыть Terminal.app, продолжаю в текущем терминале.");
            return true;
        }

        return false;
    }

    struct FaceSelection {
        std::unique_ptr<yuka::Face> face;
        yuka::SessionContext        session;
    };

    FaceSelection terminal_selection(yuka::SessionInterface interface) {
        return {std::make_unique<yuka::TerminalFace>(), {interface, {}}};
    }

    // Picks the face for this session according to $YUKA_FACE:
    //   unset / "auto" – gui/yuka-agent-desktop-ui next to the binary, else the terminal
    //   "terminal"     – always the terminal
    //   "attach"       – wait for an externally started face on the dev socket
    // Returns std::nullopt if the user cancelled with Ctrl+C.
    std::optional<FaceSelection> select_face() {
        const char*       env  = std::getenv("YUKA_FACE");
        const std::string mode = (env != nullptr && *env != '\0') ? env : "auto";

        if (mode == "terminal") {
            const bool fallback = std::getenv(k_env_terminal_fallback) != nullptr;
            return terminal_selection(fallback ? yuka::SessionInterface::terminal_backup
                                               : yuka::SessionInterface::terminal_chosen);
        }

        if (mode == "attach") {
            yuka::term::print_info(std::string("Режим разработки: жду подключения мордашки к ") +
                                   yuka::k_dev_socket_path + " (Ctrl+C — выход)...");
            auto face = yuka::SocketFace::attach(yuka::k_dev_socket_path);
            if (!face) {
                return std::nullopt;
            }
            yuka::term::print_info("Мордашка подключилась.");
            return FaceSelection{std::move(face),
                                 {yuka::SessionInterface::face, "мордашка в режиме разработки"}};
        }

        if (mode != "auto") {
            yuka::term::print_error("неизвестное значение YUKA_FACE=" + mode +
                                    " (допустимо: auto, terminal, attach), ищу GUI как обычно.");
        }

        // GUI: <binary dir>/gui/yuka-agent-desktop-ui; otherwise the terminal face.
        const auto dir = yuka::gui_dir();
        const auto gui = yuka::find_gui(dir);
        if (!gui) {
            yuka::term::print_info("В " + dir.string() + " нет " + yuka::k_gui_binary_name + ".");
            return terminal_selection(yuka::SessionInterface::terminal_backup);
        }

        yuka::term::print_info("Нашла мордашку «" + gui->name + "» (" + gui->title +
                               "), запускаю...");
        try {
            auto face = yuka::SocketFace::launch(*gui, k_face_connect_timeout);
            if (!face) {
                return std::nullopt; // Ctrl+C while waiting
            }
            return FaceSelection{std::move(face), {yuka::SessionInterface::face, gui->title}};
        } catch (const std::exception& ex) {
            yuka::term::print_error("мордашка «" + gui->name + "» не запустилась: " + ex.what());
        }
        return terminal_selection(yuka::SessionInterface::terminal_backup);
    }

    // Keeps the system prompt (index 0) and the most recent messages.
    void trim_history(std::vector<yuka::ChatMessage>& history) {
        if (history.size() <= k_max_history_messages + 1) {
            return;
        }
        const auto excess = history.size() - (k_max_history_messages + 1);
        history.erase(history.begin() + 1, history.begin() + 1 + static_cast<long>(excess));
    }

    void run_chat(yuka::Face&                 face,
                  const yuka::SessionContext& session,
                  const yuka::OllamaClient&   client,
                  const yuka::AgentConfig&    config) {
        // The greeting depends on how the user reaches Yuka in this session.
        const std::string greeting = yuka::compose_greeting(config, session);

        std::vector<yuka::ChatMessage> history;
        history.push_back({"system", yuka::build_system_prompt(config, session, greeting)});
        history.push_back({"assistant", greeting});

        face.start_session({config.name, config.model}, greeting);

        while (!yuka::stop_requested() && face.is_alive()) {
            const auto input = face.read_user_message();
            if (!input) {
                break;
            }
            if (input->empty()) {
                continue;
            }

            history.push_back({"user", *input});
            face.begin_reply();

            try {
                const std::string reply = client.chat(history, [&](const std::string& chunk) {
                    face.reply_chunk(chunk);
                    return !yuka::stop_requested() && face.is_alive();
                });
                face.end_reply();

                if (yuka::stop_requested() || !face.is_alive()) {
                    break;
                }
                history.push_back({"assistant", reply});
                trim_history(history);
            } catch (const std::exception& ex) {
                history.pop_back(); // drop the unanswered user message
                if (yuka::stop_requested()) {
                    break;
                }
                face.show_error(ex.what());
            }
        }
    }

    bool load_config(yuka::AgentConfig& config) {
        const auto config_path = yuka::resolve_storage_dir() / yuka::k_config_file_name;
        try {
            config = yuka::load_agent_config(config_path);
            return true;
        } catch (const std::exception& ex) {
            yuka::term::print_error(std::string("не удалось загрузить конфигурацию агента: ") +
                                    ex.what());
            yuka::term::print_info(std::string("Положи ") + yuka::k_config_file_name +
                                   " в каталог " + yuka::k_storage_dir_name +
                                   "/ рядом с yuka-agent или укажи каталог через YUKA_STORAGE.");
            return false;
        }
    }

} // namespace

int main() {
    yuka::install_signal_handlers();

    // Validate the config before anything else so a broken file fails fast in this terminal.
    yuka::AgentConfig config;
    if (!load_config(config)) {
        return 1;
    }

    // Ollama must already be running with the configured model; it is never started here.
    const yuka::OllamaClient client(k_ollama_base_url, config.model, config.options);
    if (!check_ollama(client)) {
        return 1;
    }

    auto selection = select_face();
    if (!selection) {
        std::cout << "\n" << yuka::term::info << "До встречи!" << yuka::term::reset << "\n";
        return 0;
    }

    const bool is_gui = selection->session.interface == yuka::SessionInterface::face;
    if (!is_gui && !ensure_system_terminal(selection->session)) {
        return 0;
    }

    if (is_gui) {
        // The greeting and the conversation live in the window; the terminal shows status only.
        yuka::term::print_info(config.name +
                               " открыта в отдельном окне. Закрой окно или нажми Ctrl+C, "
                               "чтобы выйти.");
    }

    run_chat(*selection->face, selection->session, client, config);

    selection->face.reset(); // say goodbye to the GUI and wait for it to close
    std::cout << "\n" << yuka::term::info << "До встречи!" << yuka::term::reset << "\n";
    return 0;
}
