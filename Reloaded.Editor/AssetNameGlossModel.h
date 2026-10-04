#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// English glosses for French asset names. Nothing is renamed: a name is split
// into words, each word is looked up in a French->English dictionary, and the
// browsers show "Porte_Metal01 (door metal 01)". Searches match the original
// name or its gloss, so "door" finds Porte_Metal01 and "porte" still does.
//
// Pure logic (no Windows, no engine) so tests/AssetNameGlossModelTests.cpp can
// run it on its own. The dictionary text format is described at the top of
// tools/asset_names/fr-en.txt.
namespace AssetNames
{
enum class Kind
{
    Noun,      // a trailing s/x makes the English plural
    Adjective, // plurals keep the English unchanged
    English    // an English word that may be glued to a French one
};

struct Entry
{
    std::string english;
    Kind kind = Kind::Noun;
    bool context = false; // also an English word: only beside another French word
};

// Lowercases and strips the accents of Windows-1252 text (the editor's ANSI
// names): "G\xE9n\xE9rateur" -> "generateur". Characters other than letters and
// digits are kept as they are.
inline char FoldChar(unsigned char c, bool lower)
{
    static const char* const kUpper = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs";
    static const char* const kLower = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
    char out = static_cast<char>(c);
    if (c >= 0xC0 && c <= 0xDF)
        out = kUpper[c - 0xC0];
    else if (c >= 0xE0)
        out = kLower[c - 0xE0];
    else if (c == 0x8A || c == 0x9A)
        out = c == 0x8A ? 'S' : 's';
    else if (c == 0x8E || c == 0x9E)
        out = c == 0x8E ? 'Z' : 'z';
    else if (c == 0x9F)
        out = 'Y';
    if (lower && out >= 'A' && out <= 'Z')
        out = static_cast<char>(out - 'A' + 'a');
    return out;
}

inline std::string Fold(std::string_view text, bool lower = true)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text)
    {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == 0x8C || c == 0x9C) // OE ligature
            out += (c == 0x8C && !lower) ? "OE" : "oe";
        else if (c == 0xC6 || c == 0xE6) // AE ligature
            out += (c == 0xC6 && !lower) ? "AE" : "ae";
        else
            out += FoldChar(c, lower);
    }
    return out;
}

inline bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }
inline bool IsUpper(char c) { return c >= 'A' && c <= 'Z'; }
inline bool IsLower(char c) { return c >= 'a' && c <= 'z'; }

inline std::string Trim(std::string_view text)
{
    size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return std::string(text.substr(begin, end - begin));
}

class Dictionary
{
public:
    // Adds the entries of one dictionary text; later texts win, so the user's
    // file is loaded after the built-in one. "word =" removes a word.
    void Load(std::string_view text)
    {
        Kind kind = Kind::Noun;
        bool context = false;
        size_t pos = 0;
        while (pos <= text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string_view::npos)
                end = text.size();
            std::string line = Trim(text.substr(pos, end - pos));
            pos = end + 1;
            if (line.empty() || line[0] == '#')
                continue;
            if (line[0] == '[')
            {
                const std::string header = Fold(line);
                context = header.find("context") != std::string::npos;
                if (header.find("english") != std::string::npos)
                    kind = Kind::English;
                else if (header.find("adjective") != std::string::npos)
                    kind = Kind::Adjective;
                else
                    kind = Kind::Noun;
                continue;
            }
            const size_t equals = line.find('=');
            std::string word = Fold(Trim(std::string_view(line).substr(0, equals)));
            if (word.empty() || !std::all_of(word.begin(), word.end(), IsLower))
                continue;
            if (kind == Kind::English && equals == std::string::npos)
            {
                words_[word] = Entry{word, Kind::English, false};
                continue;
            }
            if (equals == std::string::npos)
                continue;
            std::string english = Trim(std::string_view(line).substr(equals + 1));
            const size_t comment = english.find('#');
            if (comment != std::string::npos)
                english = Trim(std::string_view(english).substr(0, comment));
            if (english.empty())
            {
                words_.erase(word);
                continue;
            }
            words_[word] = Entry{english, kind, context};
        }
    }

    const Entry* Find(std::string_view word) const
    {
        const auto it = words_.find(std::string(word));
        return it == words_.end() ? nullptr : &it->second;
    }

    // Entries whose English differs from the French word.
    size_t Translations() const
    {
        size_t count = 0;
        for (const auto& [word, entry] : words_)
            if (entry.kind != Kind::English && entry.english != word)
                ++count;
        return count;
    }

    size_t Size() const { return words_.size(); }
    void Clear() { words_.clear(); }

private:
    std::unordered_map<std::string, Entry> words_;
};

struct Token
{
    std::string text; // accents folded, case kept
    bool number = false;
};

// Splits at anything that is not a letter or digit, at letter/digit changes
// and at CamelCase: "IND03_BuroChaise" -> IND, 03, Buro, Chaise and
// "HTMLPanel" -> HTML, Panel.
inline std::vector<Token> Tokenize(std::string_view name)
{
    const std::string text = Fold(name, false);
    std::vector<Token> tokens;
    size_t i = 0;
    while (i < text.size())
    {
        if (!IsAlpha(text[i]) && !IsDigit(text[i]))
        {
            ++i;
            continue;
        }
        size_t j = i + 1;
        if (IsDigit(text[i]))
        {
            while (j < text.size() && IsDigit(text[j]))
                ++j;
            tokens.push_back({text.substr(i, j - i), true});
            i = j;
            continue;
        }
        while (j < text.size() && IsAlpha(text[j]))
        {
            const char previous = text[j - 1], current = text[j];
            if (IsLower(previous) && IsUpper(current))
                break;
            if (IsUpper(previous) && IsUpper(current) && j + 1 < text.size() && IsLower(text[j + 1]) &&
                j - i >= 1)
                break;
            ++j;
        }
        tokens.push_back({text.substr(i, j - i), false});
        i = j;
    }
    return tokens;
}

inline std::string Pluralize(const std::string& english)
{
    const size_t space = english.rfind(' ');
    const std::string head = space == std::string::npos ? std::string() : english.substr(0, space + 1);
    std::string word = space == std::string::npos ? english : english.substr(space + 1);
    auto endsWith = [&](const char* suffix) {
        const size_t length = std::char_traits<char>::length(suffix);
        return word.size() >= length && word.compare(word.size() - length, length, suffix) == 0;
    };
    if (word.empty() || endsWith("s"))
        return english;
    if (endsWith("ch") || endsWith("sh") || endsWith("x") || endsWith("z"))
        word += "es";
    else if (word.size() > 1 && endsWith("y") && std::string_view("aeiou").find(word[word.size() - 2]) == std::string_view::npos)
        word = word.substr(0, word.size() - 1) + "ies";
    else
        word += "s";
    return head + word;
}

// One word looked up: directly, as a plural, or as French words glued
// together (BoisPlanche, MetalPorte).
struct WordMatch
{
    std::string english;
    bool known = false;      // in the dictionary (perhaps as an English word)
    bool translated = false; // a French word (or glued words) with a different English
    bool strong = false;     // at least one part is not a context-only word
    bool context = false;    // needs another French word in the name
    bool plural = false;     // found through its singular
    bool englishWord = false;
};

namespace Detail
{
inline bool LookupPart(const Dictionary& dictionary, const std::string& word, WordMatch& out)
{
    if (const Entry* entry = dictionary.Find(word))
    {
        out.english = entry->kind == Kind::English ? word : entry->english;
        out.known = true;
        out.englishWord = entry->kind == Kind::English;
        out.translated = entry->kind != Kind::English && entry->english != word;
        out.context = entry->context;
        out.strong = out.translated && !entry->context;
        return true;
    }
    // French plurals: a trailing s, or x after au/eu/ou (tuyaux, feux, bijoux);
    // a word already ending in s, x or z does not take another (bas, gris).
    if (word.size() < 4 || (word.back() != 's' && word.back() != 'x'))
        return false;
    const std::string singular = word.substr(0, word.size() - 1);
    const char last = singular.back();
    if (last == 's' || last == 'x' || last == 'z')
        return false;
    if (word.back() == 'x' && !(singular.size() >= 2 && (singular.compare(singular.size() - 2, 2, "au") == 0 ||
                                                          singular.compare(singular.size() - 2, 2, "eu") == 0 ||
                                                          singular.compare(singular.size() - 2, 2, "ou") == 0)))
        return false;
    const Entry* entry = dictionary.Find(singular);
    if (!entry || entry->kind == Kind::English)
        return false;
    out.english = entry->kind == Kind::Noun ? Pluralize(entry->english) : entry->english;
    out.known = true;
    out.plural = true;
    out.translated = out.english != word;
    out.context = entry->context;
    out.strong = out.translated && !entry->context;
    return true;
}

inline bool Split(const Dictionary& dictionary, const std::string& word, WordMatch& out)
{
    // Fewest parts covering the word, then fewest plurals (CaissesTruc loses to
    // CaisseStruc). French parts are at least 3 letters and English ones 4, so
    // "secured" and "sacred" are not secu+red and sac+red.
    constexpr size_t kMinPart = 3, kMinEnglishPart = 4;
    constexpr int kMaxParts = 4, kPartCost = 16, kNone = 1 << 20;
    const size_t n = word.size();
    std::vector<int> cost(n + 1, kNone), parts(n + 1, 0), from(n + 1, -1);
    std::vector<WordMatch> part(n + 1);
    cost[0] = 0;
    for (size_t end = kMinPart; end <= n; ++end)
        for (size_t begin = 0; begin + kMinPart <= end; ++begin)
        {
            if (cost[begin] == kNone || parts[begin] >= kMaxParts)
                continue;
            WordMatch piece;
            if (!LookupPart(dictionary, word.substr(begin, end - begin), piece))
                continue;
            if (piece.englishWord && end - begin < kMinEnglishPart)
                continue;
            const int total = cost[begin] + kPartCost + (piece.plural ? 1 : 0);
            if (total >= cost[end])
                continue;
            cost[end] = total;
            parts[end] = parts[begin] + 1;
            from[end] = static_cast<int>(begin);
            part[end] = piece;
        }
    if (cost[n] == kNone || parts[n] < 2)
        return false;
    std::vector<WordMatch> pieces;
    for (size_t at = n; at > 0; at = static_cast<size_t>(from[at]))
        pieces.push_back(part[at]);
    std::reverse(pieces.begin(), pieces.end());
    WordMatch result;
    result.known = true;
    for (const WordMatch& piece : pieces)
    {
        result.english += (result.english.empty() ? "" : " ") + piece.english;
        result.strong = result.strong || piece.strong;
        result.translated = result.translated || piece.translated;
    }
    if (!result.strong)
        return false;
    out = result;
    return true;
}
} // namespace Detail

inline WordMatch LookupWord(const Dictionary& dictionary, const std::string& lowerWord)
{
    WordMatch match;
    if (lowerWord.size() < 2)
        return match;
    if (Detail::LookupPart(dictionary, lowerWord, match))
        return match;
    match = {};
    if (lowerWord.size() >= 6 && Detail::Split(dictionary, lowerWord, match))
        return match;
    return {};
}

// "Mur_Beton_Sale02" -> "wall concrete dirty 02"; "" when no word was
// translated, so English names show no gloss.
inline std::string Gloss(const Dictionary& dictionary, std::string_view name)
{
    const std::vector<Token> tokens = Tokenize(name);
    std::vector<WordMatch> matches(tokens.size());
    bool anyStrong = false;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        if (tokens[i].number)
            continue;
        matches[i] = LookupWord(dictionary, Fold(tokens[i].text));
        anyStrong = anyStrong || matches[i].strong;
    }
    if (!anyStrong)
        return {};
    std::string out;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        // Dictionary words read in lower case (context-only ones too: a name
        // with a strong French word is French); others stay as written.
        if (!out.empty())
            out += ' ';
        out += matches[i].known ? matches[i].english : tokens[i].text;
    }
    return out;
}

inline std::string Label(std::string_view name, std::string_view gloss)
{
    std::string out(name);
    if (!gloss.empty())
        out.append(" (").append(gloss).append(")");
    return out;
}

// Case- and accent-insensitive. The whole query may match the name as one
// piece (the stock behaviour); otherwise every space-separated word of it
// must appear in the name or in its gloss: "metal door" finds Porte_Metal01.
inline bool MatchesGloss(std::string_view name, std::string_view gloss, std::string_view query)
{
    const std::string folded = Fold(Trim(query));
    if (folded.empty())
        return true;
    const std::string foldedName = Fold(name);
    if (foldedName.find(folded) != std::string::npos)
        return true;
    const std::string foldedGloss = Fold(gloss);
    size_t pos = 0;
    bool any = false;
    while (pos < folded.size())
    {
        while (pos < folded.size() && folded[pos] == ' ')
            ++pos;
        size_t end = folded.find(' ', pos);
        if (end == std::string::npos)
            end = folded.size();
        if (end > pos)
        {
            const std::string term = folded.substr(pos, end - pos);
            if (foldedName.find(term) == std::string::npos && foldedGloss.find(term) == std::string::npos)
                return false;
            any = true;
        }
        pos = end;
    }
    return any;
}

inline bool Matches(const Dictionary& dictionary, std::string_view name, std::string_view query)
{
    return MatchesGloss(name, Gloss(dictionary, name), query);
}
} // namespace AssetNames
