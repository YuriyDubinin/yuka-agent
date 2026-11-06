#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace yuka {

    // Who Yuka is talking to.
    struct UserProfile {
        std::string name;
        std::string relationship; // optional: how Yuka relates to the user
    };

    // Greeting parts; the final greeting is `base` + the variant for this session.
    // Variants may use the {face_title} placeholder.
    struct GreetingTemplates {
        std::string base;
        std::string with_face;       // a GUI face was started
        std::string without_face;    // no face found / every face failed → terminal
        std::string terminal_chosen; // optional: terminal requested via YUKA_FACE=terminal
    };

    // Base knowledge the agent loads on startup (config/default.json next to the binary).
    struct AgentConfig {
        std::string              name;
        std::string              language;
        std::string              model;
        UserProfile              user;
        std::string              identity;
        GreetingTemplates        greeting;
        std::vector<std::string> rules;
        nlohmann::json           options = nlohmann::json::object(); // Ollama generation options
    };

    // How the user reaches Yuka in this session.
    enum class SessionInterface {
        face,            // GUI face
        terminal_backup, // fell back to the terminal (no face or all faces failed)
        terminal_chosen, // terminal explicitly requested
    };

    struct SessionContext {
        SessionInterface interface = SessionInterface::terminal_backup;
        std::string      face_title; // set for SessionInterface::face
    };

    // The directory with Yuka's own files, read at startup and never written by the agent:
    // default.json (persona and settings), skills.json (skills).
    inline constexpr const char* k_config_dir_name = "config";
    inline constexpr const char* k_config_file_name = "default.json";

    // Config directory: $YUKA_CONFIG if set (the old name $YUKA_STORAGE still works),
    // otherwise <binary dir>/config.
    std::filesystem::path resolve_config_dir();

    // Reads and validates the config. Throws std::runtime_error with a readable message.
    // Placeholders {name}, {user} and {model} are expanded in all text fields.
    AgentConfig load_agent_config(const std::filesystem::path& file_path);

    // Greeting for this session (base + variant, {face_title} expanded).
    std::string compose_greeting(const AgentConfig& config, const SessionContext& session);

    // System prompt: identity, relationship, rules and facts about the current session.
    // What Yuka can do in this session besides talking. Empty by default: then the system
    // prompt is exactly the plain-chat prompt.
    struct PromptCapabilities {
        // Instructions of the connected face for its tools ("capabilities.instructions").
        std::string face_instructions;
        // The face has tools (the "interface tools" section is added even without
        // instructions).
        bool has_face_tools = false;
        // Skills as (name, description): an index, the bodies are loaded by load_skill.
        std::vector<std::pair<std::string, std::string>> skills;
    };

    std::string build_system_prompt(const AgentConfig&        config,
                                    const SessionContext&     session,
                                    const std::string&        greeting,
                                    const PromptCapabilities& capabilities = {});

} // namespace yuka
