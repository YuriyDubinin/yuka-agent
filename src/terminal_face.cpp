#include "terminal_face.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#include "signals.hpp"
#include "terminal.hpp"

namespace yuka {

    namespace term {

        void print_info(const std::string& message) {
            std::cout << info << " " << message << reset << "\n";
        }

        void print_error(const std::string& message) {
            std::cerr << error << " Ошибка: " << message << reset << "\n";
        }

    } // namespace term

    namespace {

        // Fast "typewriter" effect. Delays only between UTF-8 code points, so multibyte
        // characters (Cyrillic) are never split mid-sequence.
        void print_with_fast_typing_effect(const std::string& text) {
            constexpr auto k_delay = std::chrono::milliseconds(2);

            for (unsigned char ch : text) {
                if (stop_requested()) {
                    break;
                }
                const bool is_continuation_byte = (ch & 0xC0) == 0x80;
                if (!is_continuation_byte) {
                    std::cout.flush();
                    std::this_thread::sleep_for(k_delay);
                }
                std::cout.put(static_cast<char>(ch));
            }
            std::cout.flush();
        }

        bool is_exit_command(const std::string& input) {
            return input == "exit" || input == "quit" || input == "выход";
        }

    } // namespace

    void TerminalFace::start_session(const AgentInfo& agent, const std::string& greeting) {
        agent_name_ = agent.name;

        std::cout << term::banner_title << " " << agent.name << " — ИИ-агент (модель "
                  << agent.model << " через Ollama) " << term::reset << "\n"
                  << term::banner_body
                  << "Напишите сообщение и нажмите Enter. Для выхода введите «выход» или exit."
                  << term::reset << "\n\n";

        begin_reply();
        reply_chunk(greeting);
        end_reply();
    }

    std::optional<std::string> TerminalFace::read_user_message() {
        std::cout << term::user_prompt << " Вы: > " << term::reset << " " << std::flush;

        std::string input;
        if (!std::getline(std::cin, input)) {
            std::cout << "\n";
            return std::nullopt;
        }
        if (is_exit_command(input)) {
            return std::nullopt;
        }
        return input;
    }

    void TerminalFace::begin_reply() {
        std::cout << term::ai_prompt << " " << agent_name_ << ": > " << term::reset << " "
                  << term::ai_text << std::flush;
        reply_open_      = true;
        reply_has_text_  = false;
        ends_with_break_ = false;
    }

    void TerminalFace::reply_chunk(const std::string& text) {
        if (text.empty()) {
            return;
        }
        print_with_fast_typing_effect(text);
        reply_has_text_  = true;
        ends_with_break_ = text.back() == '\n';
    }

    void TerminalFace::end_reply() {
        if (!reply_open_) {
            return;
        }
        std::cout << term::reset;
        if (!reply_has_text_) {
            std::cout << term::info << "(пустой ответ)" << term::reset;
        }
        if (!ends_with_break_) {
            std::cout << "\n";
        }
        std::cout.flush();
        reply_open_ = false;
    }

    void TerminalFace::show_error(const std::string& message) {
        end_reply();
        term::print_error(message);
    }

} // namespace yuka
