#include "tools/tool_registry.hpp"

#include <algorithm>
#include <exception>

#include "platform/terminal.hpp"

namespace yuka {

    namespace {

        // Cuts `text` to at most `limit` bytes without splitting a UTF-8 sequence.
        std::string cap_utf8(std::string text, std::size_t limit) {
            if (text.size() <= limit) {
                return text;
            }
            std::size_t cut = limit;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
                --cut;
            }
            text.resize(cut);
            return text + "…(обрезано)";
        }

    } // namespace

    void ToolRegistry::replace_source(ToolSource source, std::vector<std::unique_ptr<Tool>> tools) {
        tools_.erase(std::remove_if(tools_.begin(),
                                    tools_.end(),
                                    [source](const Entry& entry) { return entry.source == source; }),
                     tools_.end());

        for (auto& tool : tools) {
            if (!tool) {
                continue;
            }
            const std::string& name = tool->spec().name;
            if (find(name) != nullptr) {
                term::print_error("инструмент «" + name +
                                  "» уже есть у агента, версия интерфейса пропущена.");
                continue;
            }
            tools_.push_back({source, std::move(tool)});
        }

        // Built-in tools first: they are the agent's own and come before the face's.
        std::stable_partition(tools_.begin(), tools_.end(), [](const Entry& entry) {
            return entry.source == ToolSource::builtin;
        });
    }

    std::vector<ToolSpec> ToolRegistry::specs() const {
        std::vector<ToolSpec> specs;
        specs.reserve(tools_.size());
        for (const auto& entry : tools_) {
            specs.push_back(entry.tool->spec());
        }
        return specs;
    }

    Tool* ToolRegistry::find(const std::string& name) const {
        for (const auto& entry : tools_) {
            if (entry.tool->spec().name == name) {
                return entry.tool.get();
            }
        }
        return nullptr;
    }

    ToolResult ToolRegistry::call(const std::string& name, const nlohmann::json& arguments) const {
        Tool* tool = find(name);
        if (tool == nullptr) {
            return ToolResult::error("Инструмента «" + name + "» нет. Доступные инструменты " +
                                     "перечислены в описании tools.");
        }
        if (!arguments.is_object()) {
            return ToolResult::error("Аргументы инструмента «" + name +
                                     "» должны быть JSON-объектом.");
        }

        ToolResult result;
        try {
            result = tool->call(arguments);
        } catch (const std::exception& ex) {
            result = ToolResult::error("Инструмент «" + name + "» упал: " + ex.what());
        }
        result.text = cap_utf8(std::move(result.text), k_max_result_bytes);
        return result;
    }

} // namespace yuka
