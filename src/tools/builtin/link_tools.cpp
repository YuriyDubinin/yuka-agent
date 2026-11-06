#include "tools/builtin/link_tools.hpp"

#include <nlohmann/json.hpp>
#include <set>

#include "platform/macos_apps.hpp"
#include "tools/builtin/name_match.hpp"

namespace yuka {

    namespace {

        using nlohmann::json;

        std::string join(const std::vector<std::string>& items) {
            std::string out;
            for (const auto& item : items) {
                out += (out.empty() ? "" : ", ") + item;
            }
            return out;
        }

        class LinkOpenTool final : public Tool {
        public:
            explicit LinkOpenTool(const AccessConfig& access) : access_(access) {
                // Every way to call a page – id, name, aliases – points back to that page.
                std::string list;
                for (std::size_t i = 0; i < access.links().size(); ++i) {
                    const AccessLink& link = access.links()[i];
                    for (const std::string* text : {&link.id, &link.name}) {
                        spellings_.push_back(*text);
                        owners_.push_back(i);
                    }
                    for (const auto& alias : link.aliases) {
                        spellings_.push_back(alias);
                        owners_.push_back(i);
                    }
                    list += "\n- " + link.id + " — " + link.name +
                            (link.aliases.empty() ? "" : " (" + join(link.aliases) + ")");
                }

                spec_.name        = "link_open";
                spec_.description = "Открывает сайт в браузере. Вызывай на «открой ютуб», "
                                    "«открой вк» и т. п. Это не проекты и не программы: проект "
                                    "открывает project_open, программу — app_open. Адрес "
                                    "передавать не нужно — только имя страницы. Страницы:" +
                                    list;
                spec_.parameters  = {
                    {"type", "object"},
                    {"properties",
                     {{"name",
                       {{"type", "string"},
                        {"description", "Имя страницы — как назвал Юра или из списка"}}}}},
                    {"required", json::array({"name"})},
                    {"additionalProperties", false},
                };
            }

            const ToolSpec&    spec() const override { return spec_; }
            const std::string& title() const override { return title_; }
            ToolAnnotations    annotations() const override { return {false, false, true}; }

            ToolResult call(const json& arguments) override {
                const auto        it    = arguments.find("name");
                const std::string query = it != arguments.end() && it->is_string()
                                              ? it->get<std::string>()
                                              : std::string();

                const NameMatch match = match_name(query, spellings_);
                if (match.kind == NameMatch::Kind::found) {
                    return open(link_for(match.name));
                }

                // Several spellings may belong to one page ("вк" and "vk"): that is not ambiguous.
                std::set<std::size_t> pages;
                for (const auto& candidate : match.candidates) {
                    pages.insert(owner_of(candidate));
                }
                if (match.kind == NameMatch::Kind::ambiguous && pages.size() == 1) {
                    return open(access_.links()[*pages.begin()]);
                }

                std::vector<std::string> names;
                for (const std::size_t page : pages) {
                    names.push_back(access_.links()[page].name);
                }
                if (match.kind == NameMatch::Kind::ambiguous) {
                    return ToolResult::error("Под «" + query + "» подходят несколько страниц: " +
                                             join(names) + ". Уточни у Юры, какую.");
                }
                std::vector<std::string> all;
                for (const auto& link : access_.links()) {
                    all.push_back(link.name);
                }
                return ToolResult::error("Такой страницы нет. Есть: " + join(all) +
                                         ". Добавить новую можно в config/access.json.");
            }

        private:
            std::size_t owner_of(const std::string& spelling) const {
                for (std::size_t i = 0; i < spellings_.size(); ++i) {
                    if (spellings_[i] == spelling) {
                        return owners_[i];
                    }
                }
                return 0;
            }

            const AccessLink& link_for(const std::string& spelling) const {
                return access_.links()[owner_of(spelling)];
            }

            ToolResult open(const AccessLink& link) const {
                const AccessApp* browser = access_.find_app(link.open_with);
                if (browser == nullptr || !macos::open_url(browser->bundle_id, link.url)) {
                    return ToolResult::error("Не удалось открыть " + link.name + ".");
                }
                return ToolResult::ok("Открыла " + link.name + " в " + browser->name + ".");
            }

            const AccessConfig&      access_;
            ToolSpec                 spec_;
            std::string              title_ = "Открыть страницу";
            std::vector<std::string> spellings_; // id, name and aliases of every page
            std::vector<std::size_t> owners_;    // spellings_[i] belongs to links()[owners_[i]]
        };

    } // namespace

    std::vector<std::unique_ptr<Tool>> make_link_tools(const AccessConfig& access) {
        std::vector<std::unique_ptr<Tool>> tools;
        if (!access.links().empty()) {
            tools.push_back(std::make_unique<LinkOpenTool>(access));
        }
        return tools;
    }

} // namespace yuka
