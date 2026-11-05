#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "face_discovery.hpp"

namespace yuka {

    namespace {

        using nlohmann::json;

        const json& require_object(const json& root, const std::string& key) {
            const auto it = root.find(key);
            if (it == root.end()) {
                throw std::runtime_error("missing required field '" + key + "'");
            }
            if (!it->is_object()) {
                throw std::runtime_error("field '" + key + "' must be an object");
            }
            return *it;
        }

        std::string require_string(const json& root, const std::string& key) {
            const auto it = root.find(key);
            if (it == root.end()) {
                throw std::runtime_error("missing required field '" + key + "'");
            }
            if (!it->is_string()) {
                throw std::runtime_error("field '" + key + "' must be a string");
            }

            std::string value = it->get<std::string>();
            if (value.empty()) {
                throw std::runtime_error("field '" + key + "' must not be empty");
            }
            return value;
        }

        std::string optional_string(const json& root, const std::string& key) {
            const auto it = root.find(key);
            if (it == root.end()) {
                return {};
            }
            if (!it->is_string()) {
                throw std::runtime_error("field '" + key + "' must be a string");
            }
            return it->get<std::string>();
        }

        std::vector<std::string> optional_string_array(const json& root, const std::string& key) {
            std::vector<std::string> result;

            const auto it = root.find(key);
            if (it == root.end()) {
                return result;
            }
            if (!it->is_array()) {
                throw std::runtime_error("field '" + key + "' must be an array");
            }

            for (const auto& item : *it) {
                if (!item.is_string()) {
                    throw std::runtime_error("field '" + key + "' must contain only strings");
                }
                result.push_back(item.get<std::string>());
            }
            return result;
        }

        void replace_all(std::string& text, const std::string& from, const std::string& to) {
            if (from.empty()) {
                return;
            }
            std::size_t pos = 0;
            while ((pos = text.find(from, pos)) != std::string::npos) {
                text.replace(pos, from.size(), to);
                pos += to.size();
            }
        }

        // Substitutes {name}, {user} and {model} with config values.
        void expand_placeholders(std::string& text, const AgentConfig& config) {
            replace_all(text, "{name}", config.name);
            replace_all(text, "{user}", config.user.name);
            replace_all(text, "{model}", config.model);
        }

        std::string join_sentences(const std::string& first, const std::string& second) {
            if (first.empty()) {
                return second;
            }
            if (second.empty()) {
                return first;
            }
            return first + " " + second;
        }

        std::string describe_interface(const SessionContext& session) {
            switch (session.interface) {
                case SessionInterface::face:
                    return "графическую мордашку — " + session.face_title +
                           " (отдельное окно чата)";
                case SessionInterface::terminal_backup:
                    return "системный терминал (графические мордашки не нашлись или не "
                           "запустились)";
                case SessionInterface::terminal_chosen:
                    return "системный терминал";
            }
            return "системный терминал";
        }

    } // namespace

    std::filesystem::path resolve_storage_dir() {
        if (const char* env = std::getenv("YUKA_STORAGE"); env != nullptr && *env != '\0') {
            return std::filesystem::path(env);
        }
        return executable_dir() / k_storage_dir_name;
    }

    AgentConfig load_agent_config(const std::filesystem::path& file_path) {
        std::ifstream input(file_path);
        if (!input) {
            throw std::runtime_error("cannot open config file: " + file_path.string());
        }

        json root;
        try {
            root = json::parse(input);
        } catch (const json::parse_error& ex) {
            throw std::runtime_error("invalid JSON in " + file_path.string() + ": " + ex.what());
        }

        if (!root.is_object()) {
            throw std::runtime_error("config root must be a JSON object: " + file_path.string());
        }

        try {
            AgentConfig config;
            config.name     = require_string(root, "name");
            config.language = require_string(root, "language");
            config.model    = require_string(root, "model");
            config.identity = require_string(root, "identity");
            config.rules    = optional_string_array(root, "rules");

            const json& user         = require_object(root, "user");
            config.user.name         = require_string(user, "name");
            config.user.relationship = optional_string(user, "relationship");

            const json& greeting            = require_object(root, "greeting");
            config.greeting.base            = require_string(greeting, "base");
            config.greeting.with_face       = require_string(greeting, "with_face");
            config.greeting.without_face    = require_string(greeting, "without_face");
            config.greeting.terminal_chosen = optional_string(greeting, "terminal_chosen");

            if (const auto opts = root.find("options"); opts != root.end()) {
                if (!opts->is_object()) {
                    throw std::runtime_error("field 'options' must be an object");
                }
                config.options = *opts;
            }

            for (std::string* text : {&config.identity,
                                      &config.user.relationship,
                                      &config.greeting.base,
                                      &config.greeting.with_face,
                                      &config.greeting.without_face,
                                      &config.greeting.terminal_chosen}) {
                expand_placeholders(*text, config);
            }
            for (auto& rule : config.rules) {
                expand_placeholders(rule, config);
            }
            return config;
        } catch (const std::exception& ex) {
            throw std::runtime_error(file_path.string() + ": " + ex.what());
        }
    }

    std::string compose_greeting(const AgentConfig& config, const SessionContext& session) {
        std::string variant;
        switch (session.interface) {
            case SessionInterface::face:
                variant = config.greeting.with_face;
                break;
            case SessionInterface::terminal_backup:
                variant = config.greeting.without_face;
                break;
            case SessionInterface::terminal_chosen:
                variant = config.greeting.terminal_chosen;
                break;
        }
        replace_all(variant, "{face_title}", session.face_title);
        return join_sentences(config.greeting.base, variant);
    }

    std::string build_system_prompt(const AgentConfig&    config,
                                    const SessionContext& session,
                                    const std::string&    greeting) {
        std::ostringstream prompt;

        prompt << config.identity << "\n";
        if (!config.user.relationship.empty()) {
            prompt << config.user.relationship << "\n";
        }

        if (!config.rules.empty()) {
            prompt << "\nПравила, которые ты обязана соблюдать:\n";
            for (std::size_t i = 0; i < config.rules.size(); ++i) {
                prompt << i + 1 << ". " << config.rules[i] << "\n";
            }
        }

        prompt << "\nТекущая сессия:\n"
               << "- Ты общаешься с " << config.user.name << ".\n"
               << "- Вы общаетесь через " << describe_interface(session) << ".\n"
               << "- В начале разговора ты уже поздоровалась так: \"" << greeting << "\"\n";

        return prompt.str();
    }

} // namespace yuka
