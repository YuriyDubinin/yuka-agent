#pragma once

#include <string>
#include <vector>

namespace yuka {

    // Finding a name the way Yura says it: "открой проект Альфа" must find the folder "Alfa",
    // "закрой альфу" too, and two similar names must not be guessed between.
    // Pure functions, no I/O.

    // Lower case (Cyrillic too), Cyrillic → Latin, separators dropped, and a few spelling
    // equivalences folded (c/k, z/s, w/v, doubled letters), so "Альфа", "ALFA" and "alfa"
    // are the same and "казино" meets "Casino".
    std::string normalize_name(const std::string& text);

    struct NameMatch {
        enum class Kind { found, ambiguous, not_found };

        Kind                     kind = Kind::not_found;
        std::string              name;        // found: the candidate
        std::vector<std::string> candidates;  // ambiguous: all that fit; not_found: closest
    };

    // Rules, in order (the first one that matches anything decides):
    //   1. equal after normalization
    //   2. starts with the query (also without its last letter: "альфу" → "альф")
    //   3. contains the query
    // One match → found; several → ambiguous; none → up to 5 closest by edit distance.
    NameMatch match_name(const std::string& query, const std::vector<std::string>& names);

} // namespace yuka
