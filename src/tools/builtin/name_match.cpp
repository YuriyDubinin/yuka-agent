#include "tools/builtin/name_match.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace yuka {

    namespace {

        // Decodes one UTF-8 code point at `i` and advances `i`. Invalid bytes → U+FFFD.
        char32_t next_code_point(const std::string& s, std::size_t& i) {
            const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
            const unsigned char c = byte(i);
            if (c < 0x80) {
                ++i;
                return c;
            }
            const int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
            if (len == 0 || i + static_cast<std::size_t>(len) > s.size()) {
                ++i;
                return 0xFFFD;
            }
            char32_t cp = c & (0x7F >> len);
            for (int k = 1; k < len; ++k) {
                cp = (cp << 6) | (byte(i + static_cast<std::size_t>(k)) & 0x3F);
            }
            i += static_cast<std::size_t>(len);
            return cp;
        }

        // Russian Cyrillic (lower case) → Latin, as people type project names.
        const char* transliterate(char32_t cp) {
            static const char* const table[] = {
                "a", "b", "v", "g", "d", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p",
                "r", "s", "t", "u", "f", "h", "c", "ch", "sh", "sch", "", "y", "", "e", "yu", "ya",
            };
            if (cp >= 0x0430 && cp <= 0x044F) { // а..я
                return table[cp - 0x0430];
            }
            if (cp == 0x0451) { // ё
                return "e";
            }
            return nullptr;
        }

        // Spelling equivalences after transliteration, applied to both sides.
        std::string fold(const std::string& s) {
            std::string out;
            for (std::size_t i = 0; i < s.size(); ++i) {
                char c = s[i];
                if (c == 'c' && i + 1 < s.size() && s[i + 1] == 'k') {
                    continue; // "ck" → "k"
                }
                if (c == 'c' || c == 'q') {
                    c = 'k';
                } else if (c == 'z') {
                    c = 's';
                } else if (c == 'w') {
                    c = 'v';
                } else if (c == 'x') {
                    out += "ks";
                    continue;
                }
                if (!out.empty() && out.back() == c) {
                    continue; // doubled letters
                }
                out += c;
            }
            return out;
        }

        std::size_t edit_distance(const std::string& a, const std::string& b) {
            std::vector<std::size_t> row(b.size() + 1);
            for (std::size_t j = 0; j <= b.size(); ++j) {
                row[j] = j;
            }
            for (std::size_t i = 1; i <= a.size(); ++i) {
                std::size_t diagonal = row[0];
                row[0]               = i;
                for (std::size_t j = 1; j <= b.size(); ++j) {
                    const std::size_t above = row[j];
                    row[j] = std::min({row[j] + 1, row[j - 1] + 1,
                                       diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
                    diagonal = above;
                }
            }
            return row[b.size()];
        }

    } // namespace

    std::string normalize_name(const std::string& text) {
        std::string latin;
        for (std::size_t i = 0; i < text.size();) {
            char32_t cp = next_code_point(text, i);
            if (cp >= 'A' && cp <= 'Z') {
                cp += 'a' - 'A';
            } else if (cp >= 0x0410 && cp <= 0x042F) { // А..Я
                cp += 0x20;
            } else if (cp == 0x0401) { // Ё
                cp = 0x0451;
            }

            if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) {
                latin += static_cast<char>(cp);
            } else if (const char* t = transliterate(cp)) {
                latin += t;
            }
            // Everything else (spaces, -, _, ., punctuation) is a separator: dropped.
        }
        return fold(latin);
    }

    NameMatch match_name(const std::string& query, const std::vector<std::string>& names) {
        NameMatch   result;
        std::string q = normalize_name(query);
        if (q.empty()) {
            result.candidates.assign(names.begin(), names.begin() + std::min<std::size_t>(5, names.size()));
            return result;
        }

        std::vector<std::pair<std::string, std::string>> normalized; // (name, normalized)
        for (const auto& name : names) {
            normalized.emplace_back(name, normalize_name(name));
        }

        const auto decide = [&](const std::function<bool(const std::string&)>& fits) {
            std::vector<std::string> hits;
            for (const auto& [name, norm] : normalized) {
                if (fits(norm)) {
                    hits.push_back(name);
                }
            }
            if (hits.size() == 1) {
                result.kind = NameMatch::Kind::found;
                result.name = hits.front();
            } else if (hits.size() > 1) {
                result.kind       = NameMatch::Kind::ambiguous;
                result.candidates = std::move(hits);
            }
            return !result.candidates.empty() || result.kind == NameMatch::Kind::found;
        };

        // A spoken case ending: "альфу" ↔ "alfa", "юку" ↔ "yuka" – the same word with
        // another final vowel. Only the last letter may differ, and only vowel for vowel.
        const auto is_vowel      = [](char c) { return std::string("aeiouy").find(c) != std::string::npos; };
        const auto case_ending   = [&](const std::string& n) {
            return q.size() > 3 && n.size() == q.size() && is_vowel(q.back()) &&
                   is_vowel(n.back()) && n.compare(0, q.size() - 1, q, 0, q.size() - 1) == 0;
        };

        if (decide([&](const std::string& n) { return n == q; }) ||
            decide([&](const std::string& n) { return n.rfind(q, 0) == 0; }) ||
            decide(case_ending) ||
            decide([&](const std::string& n) { return n.find(q) != std::string::npos; })) {
            return result;
        }

        // Nothing fits: offer the closest names.
        std::vector<std::pair<std::size_t, std::string>> scored;
        for (const auto& [name, norm] : normalized) {
            scored.emplace_back(edit_distance(q, norm), name);
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });

        // A clear winner is accepted: close enough to the query (≤ 40% of its length) and
        // well ahead of the runner-up (by ≥ 2 edits). "кибер лаб" → CyberLab, but two
        // similar names are never guessed between.
        if (!scored.empty()) {
            const std::size_t best   = scored[0].first;
            const std::size_t second = scored.size() > 1 ? scored[1].first : best + 100;
            const std::size_t limit  = std::max<std::size_t>(1, (q.size() * 2) / 5);
            if (best <= limit && second >= best + 2) {
                result.kind = NameMatch::Kind::found;
                result.name = scored[0].second;
                return result;
            }
        }

        for (std::size_t i = 0; i < scored.size() && i < 5; ++i) {
            result.candidates.push_back(scored[i].second);
        }
        result.kind = NameMatch::Kind::not_found;
        return result;
    }

} // namespace yuka
