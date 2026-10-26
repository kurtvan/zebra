#include "library/PresetParsing.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace gv::library {

auto ToLower(std::string text) -> std::string
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

namespace {

/// "shifter", "Stahlregen & Boz", "fed + flexi + geiss", "Martin" are authors; "Fast transition to black" isn't.
/// Collaborations use +, & or commas; plain phrases of more than three words are descriptions.
auto LooksLikeAuthor(const std::string& text) -> bool
{
    if (text.find_first_of("+&,") != std::string::npos)
        return true;
    int words = 1;
    for (char c : text)
        words += c == ' ' ? 1 : 0;
    return words <= 3;
}

} // namespace

auto ParsePresetPath(const std::string& relativePath) -> PathInfo
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= relativePath.size())
    {
        const size_t slash = relativePath.find('/', start);
        const size_t end = slash == std::string::npos ? relativePath.size() : slash;
        if (end > start)
            parts.push_back(relativePath.substr(start, end - start));
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }

    PathInfo info;
    if (parts.empty())
        return info;

    std::string file = parts.back();
    parts.pop_back();
    if (parts.size() >= 1)
        info.category = parts[0];
    if (parts.size() >= 2)
        info.subcategory = parts[1];

    if (const size_t dot = file.rfind('.'); dot != std::string::npos && ToLower(file.substr(dot)) == ".milk")
        file.erase(dot);

    if (const size_t separator = file.find(" - "); separator != std::string::npos && separator > 0 && LooksLikeAuthor(file.substr(0, separator)))
    {
        info.author = file.substr(0, separator);
        info.name = file.substr(separator + 3);
    }
    else
    {
        info.name = file;
    }
    if (info.name.empty())
        info.name = file;
    return info;
}

namespace {

auto IsBuiltIn(const std::string& name) -> bool
{
    if (name == "main" || name == "blur1" || name == "blur2" || name == "blur3")
        return true;
    if (name.rfind("noisevol", 0) == 0 || name.rfind("noise_", 0) == 0)
        return true;
    // rand00..rand15 (with optional suffix, e.g. rand05_smalltiled)
    return name.size() >= 6 && name.rfind("rand", 0) == 0 && std::isdigit(static_cast<unsigned char>(name[4])) &&
           std::isdigit(static_cast<unsigned char>(name[5]));
}

} // namespace

auto ExtractTextureNames(const std::string& presetText) -> std::set<std::string>
{
    std::set<std::string> names;
    const std::string text = ToLower(presetText);
    const std::string marker = "sampler_";
    for (size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at + 1))
    {
        // Must start a word ("texsampler_x" isn't a sampler).
        if (at > 0 && (std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_'))
            continue;
        size_t end = at + marker.size();
        while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
            ++end;
        std::string name = text.substr(at + marker.size(), end - at - marker.size());
        for (const char* prefix : {"fw_", "fc_", "pw_", "pc_"})
        {
            if (name.rfind(prefix, 0) == 0)
            {
                name.erase(0, 3);
                break;
            }
        }
        if (!name.empty() && !IsBuiltIn(name))
            names.insert(name);
    }
    return names;
}

} // namespace gv::library
