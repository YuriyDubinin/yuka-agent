#include "faces/face_discovery.hpp"

#include <cstdint>
#include <cstdlib>
#include <mach-o/dyld.h>
#include <unistd.h>

namespace yuka {

    namespace {

        namespace fs = std::filesystem;

        bool is_executable_file(const fs::path& path) {
            std::error_code ec;
            return fs::is_regular_file(path, ec) && access(path.c_str(), X_OK) == 0;
        }

    } // namespace

    fs::path executable_path() {
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return {};
        }
        buffer.resize(std::char_traits<char>::length(buffer.c_str()));

        std::error_code ec;
        const fs::path  resolved = fs::canonical(buffer, ec);
        return ec ? fs::absolute(buffer) : resolved;
    }

    fs::path executable_dir() {
        const fs::path self = executable_path();
        return self.empty() ? fs::current_path() : self.parent_path();
    }

    fs::path gui_dir() {
        if (const char* env = std::getenv("YUKA_GUI_DIR"); env != nullptr && *env != '\0') {
            return fs::path(env);
        }
        return executable_dir() / k_gui_dir_name;
    }

    std::optional<FaceManifest> find_gui(const fs::path& dir) {
        const std::string name = k_gui_binary_name;

        // An Electron app cannot run as a lone binary – it lives inside its .app bundle,
        // so the bundle's executable is preferred; a plain executable is accepted too.
        const fs::path candidates[] = {
            dir / (name + ".app") / "Contents" / "MacOS" / name,
            dir / name,
        };

        for (const auto& candidate : candidates) {
            if (!is_executable_file(candidate)) {
                continue;
            }
            std::error_code ec;
            fs::path        executable = fs::weakly_canonical(candidate, ec);
            if (ec) {
                executable = candidate;
            }
            return FaceManifest{name, k_gui_title, executable, {}};
        }
        return std::nullopt;
    }

} // namespace yuka
