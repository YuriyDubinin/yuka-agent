#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "faces/face.hpp"
#include "tools/tool.hpp"

namespace yuka {

    // A tool that lives in the face: calling it sends `tool_call` over the face's socket and
    // waits for `tool_result` (docs/face-protocol.md). The face must outlive the tool.
    class RemoteTool final : public Tool {
    public:
        // Waiting limits: destructive tools wait for the user's confirmation in the face.
        static constexpr std::chrono::seconds k_timeout{10};
        static constexpr std::chrono::seconds k_confirm_timeout{60};

        RemoteTool(Face& face, FaceTool tool) : face_(face), tool_(std::move(tool)) {}

        const ToolSpec&    spec() const override { return tool_.spec; }
        const std::string& title() const override { return tool_.title; }
        ToolAnnotations    annotations() const override { return tool_.annotations; }

        ToolResult call(const nlohmann::json& arguments) override {
            const auto timeout = tool_.annotations.destructive ? k_confirm_timeout : k_timeout;
            return face_.call_tool(tool_.spec.name, arguments, timeout);
        }

    private:
        Face&    face_;
        FaceTool tool_;
    };

    // Wraps every tool the face currently offers.
    inline std::vector<std::unique_ptr<Tool>> remote_tools(Face& face) {
        std::vector<std::unique_ptr<Tool>> tools;
        for (auto& tool : face.tools()) {
            tools.push_back(std::make_unique<RemoteTool>(face, std::move(tool)));
        }
        return tools;
    }

} // namespace yuka
