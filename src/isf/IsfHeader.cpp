#include "isf/IsfHeader.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <optional>

namespace gv::isf {

namespace {

using nlohmann::json;

/// The 1-based line of `offset` in `text`.
auto LineAt(std::string_view text, size_t offset) -> int
{
    offset = std::min(offset, text.size());
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
}

struct TypeName
{
    const char* name;
    InputType type;
    size_t components; //!< numbers in DEFAULT, MIN and MAX; 0 for none
};

constexpr TypeName kTypes[] = {
    {"float", InputType::Float, 1},    {"bool", InputType::Bool, 1},   {"long", InputType::Long, 1},
    {"color", InputType::Color, 4},    {"point2D", InputType::Point2D, 2}, {"event", InputType::Event, 1},
    {"image", InputType::Image, 0},    {"audio", InputType::Audio, 1}, {"audioFFT", InputType::AudioFft, 1},
};

/// `value` as `count` numbers (a number or bool when count is 1, else an array), or nothing if it isn't.
auto Numbers(const json& value, size_t count) -> std::optional<std::vector<float>>
{
    if (count == 1 && (value.is_number() || value.is_boolean()))
        return std::vector<float>{value.is_boolean() ? (value.get<bool>() ? 1.f : 0.f) : value.get<float>()};
    if (count < 2 || !value.is_array() || value.size() != count)
        return std::nullopt;
    std::vector<float> numbers;
    for (const auto& element : value)
    {
        if (!element.is_number())
            return std::nullopt;
        numbers.push_back(element.get<float>());
    }
    return numbers;
}

auto StringField(const json& object, const char* key) -> std::string
{
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

/// A yes/no field, written as true, 1 or "true" in the wild.
auto Flag(const json& object, const char* key) -> bool
{
    const auto it = object.find(key);
    if (it == object.end())
        return false;
    if (it->is_boolean())
        return it->get<bool>();
    if (it->is_number())
        return it->get<double>() != 0.0;
    return it->is_string() && it->get<std::string>() == "true";
}

/// The string list at `key`; a single string counts as a list of one.
auto StringList(const json& object, const char* key) -> std::vector<std::string>
{
    std::vector<std::string> list;
    const auto it = object.find(key);
    if (it == object.end())
        return list;
    if (it->is_string())
        list.push_back(it->get<std::string>());
    else if (it->is_array())
        for (const auto& element : *it)
            if (element.is_string())
                list.push_back(element.get<std::string>());
    return list;
}

/// Reads one INPUTS entry, or says what's wrong with it.
auto ReadInput(const json& entry, size_t index) -> std::variant<IsfInput, std::string>
{
    const std::string where = "input " + std::to_string(index + 1);
    if (!entry.is_object())
        return where + " should be a { } object";
    IsfInput input;
    input.name = StringField(entry, "NAME");
    if (input.name.empty())
        return where + " has no NAME";
    const std::string typeName = StringField(entry, "TYPE");
    const auto* type = std::find_if(std::begin(kTypes), std::end(kTypes), [&](const TypeName& t) { return typeName == t.name; });
    if (type == std::end(kTypes))
        return "input \"" + input.name + "\" has " + (typeName.empty() ? std::string("no TYPE") : "an unknown TYPE \"" + typeName + "\"");
    input.type = type->type;
    input.label = StringField(entry, "LABEL");

    const std::pair<const char*, std::vector<float>*> numberFields[] = {{"DEFAULT", &input.defaultValue}, {"MIN", &input.min}, {"MAX", &input.max}};
    for (const auto& [key, field] : numberFields)
    {
        const auto it = entry.find(key);
        if (it == entry.end() || it->is_null())
            continue;
        auto numbers = Numbers(*it, type->components);
        if (!numbers)
        {
            const char* shape = type->components == 1 ? "a number" : type->components == 2 ? "an array of 2 numbers" : "an array of 4 numbers";
            if (type->components == 0)
                return "input \"" + input.name + "\": an image input can't have " + key;
            return "input \"" + input.name + "\": " + key + " should be " + shape;
        }
        *field = std::move(*numbers);
    }

    if (input.type == InputType::Long)
    {
        input.labels = StringList(entry, "LABELS");
        if (const auto it = entry.find("VALUES"); it != entry.end() && it->is_array())
        {
            for (const auto& value : *it)
            {
                if (!value.is_number())
                    return "input \"" + input.name + "\": VALUES should be whole numbers";
                input.values.push_back(value.get<int>());
            }
        }
        else
            for (size_t i = 0; i < input.labels.size(); ++i)
                input.values.push_back(static_cast<int>(i));
    }
    return input;
}

auto HasInput(const IsfShader& shader, std::string_view name, InputType type) -> bool
{
    const auto* input = FindInput(shader, name);
    return input != nullptr && input->type == type;
}

/// Replaces every whole-word `from` in `text` with `to`.
void ReplaceWord(std::string& text, std::string_view from, std::string_view to)
{
    const auto isWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at))
    {
        const bool startsWord = at == 0 || !isWord(text[at - 1]);
        const bool endsWord = at + from.size() >= text.size() || !isWord(text[at + from.size()]);
        if (startsWord && endsWord)
        {
            text.replace(at, from.size(), to);
            at += to.size();
        }
        else
            at += from.size();
    }
}

auto UniformType(InputType type) -> const char*
{
    switch (type)
    {
        case InputType::Float: return "float";
        case InputType::Bool:
        case InputType::Event: return "bool";
        case InputType::Long: return "int";
        case InputType::Color: return "vec4";
        case InputType::Point2D: return "vec2";
        case InputType::Image:
        case InputType::Audio:
        case InputType::AudioFft: return "sampler2D";
    }
    return "float";
}

} // namespace

auto ParseIsf(std::string_view source) -> std::variant<IsfShader, IsfError>
{
    const size_t open = source.find_first_not_of(" \t\r\n");
    if (open == std::string_view::npos || source.substr(open, 2) != "/*")
        return IsfError{LineAt(source, open == std::string_view::npos ? 0 : open),
                        "no ISF header: the file should start with a /*{ ... }*/ comment holding its JSON"};
    const size_t jsonStart = open + 2;
    const size_t close = source.find("*/", jsonStart);
    if (close == std::string_view::npos)
        return IsfError{LineAt(source, open), "the header comment that starts here is never closed with */"};
    const std::string_view text = source.substr(jsonStart, close - jsonStart);

    json header;
    try
    {
        header = json::parse(text);
    }
    catch (const json::parse_error& error)
    {
        const size_t at = jsonStart + (error.byte > 0 ? error.byte - 1 : 0);
        std::string message = error.what();
        if (const size_t reason = message.find("syntax error"); reason != std::string::npos)
            message = message.substr(reason);
        return IsfError{LineAt(source, at), "the header isn't valid JSON: " + message};
    }
    if (!header.is_object())
        return IsfError{LineAt(source, jsonStart), "the header should be a JSON { } object"};

    IsfShader shader;
    shader.version = StringField(header, "ISFVSN");
    shader.description = StringField(header, "DESCRIPTION");
    shader.credit = StringField(header, "CREDIT");
    shader.categories = StringList(header, "CATEGORIES");

    if (const auto inputs = header.find("INPUTS"); inputs != header.end())
    {
        if (!inputs->is_array())
            return IsfError{LineAt(source, jsonStart + text.find("\"INPUTS\"")), "INPUTS should be a [ ] list"};
        for (size_t i = 0; i < inputs->size(); ++i)
        {
            auto input = ReadInput((*inputs)[i], i);
            if (auto* problem = std::get_if<std::string>(&input))
            {
                // Point at the input's NAME if it has one; JSON values carry no positions.
                const std::string name = (*inputs)[i].is_object() ? StringField((*inputs)[i], "NAME") : std::string();
                const size_t at = name.empty() ? std::string_view::npos : text.find("\"" + name + "\"");
                return IsfError{LineAt(source, jsonStart + (at == std::string_view::npos ? text.find("\"INPUTS\"") : at)), *problem};
            }
            const auto& read = std::get<IsfInput>(input);
            if (FindInput(shader, read.name) != nullptr)
                return IsfError{LineAt(source, jsonStart + text.find("\"" + read.name + "\"")), "two inputs are called \"" + read.name + "\""};
            shader.inputs.push_back(read);
        }
    }

    if (const auto passes = header.find("PASSES"); passes != header.end() && passes->is_array())
    {
        for (const auto& entry : *passes)
        {
            if (!entry.is_object())
                continue;
            IsfPass pass;
            pass.target = StringField(entry, "TARGET");
            pass.persistent = Flag(entry, "PERSISTENT");
            pass.floatTarget = Flag(entry, "FLOAT");
            for (const auto& [key, field] : {std::pair{"WIDTH", &pass.width}, std::pair{"HEIGHT", &pass.height}})
            {
                if (const auto it = entry.find(key); it != entry.end())
                    *field = it->is_string() ? it->get<std::string>() : it->dump();
            }
            shader.passes.push_back(pass);
        }
    }

    // A { name: { PATH } } object, or a [ { NAME, PATH } ] list in older files.
    if (const auto imported = header.find("IMPORTED"); imported != header.end())
    {
        if (imported->is_object())
        {
            for (const auto& entry : imported->items())
                shader.imported.push_back(entry.key());
        }
        else if (imported->is_array())
        {
            for (const auto& entry : *imported)
                shader.imported.push_back(entry.is_object() ? StringField(entry, "NAME") : std::string());
        }
    }

    if (HasInput(shader, "startImage", InputType::Image) && HasInput(shader, "endImage", InputType::Image) &&
        HasInput(shader, "progress", InputType::Float))
        shader.kind = ShaderKind::Transition;
    else if (HasInput(shader, "inputImage", InputType::Image))
        shader.kind = ShaderKind::Filter;
    else
        shader.kind = ShaderKind::Generator;

    shader.glsl = std::string(source.substr(close + 2));
    shader.glslLine = LineAt(source, close);
    return shader;
}

auto UnsupportedFeature(const IsfShader& shader) -> std::string
{
    // Only one pass into the output is drawn (multi-pass is D5, #72): any buffer a pass draws into would be an
    // undeclared sampler in the code that reads it.
    const bool buffers = shader.passes.size() > 1 ||
                         std::any_of(shader.passes.begin(), shader.passes.end(), [](const IsfPass& pass) { return pass.persistent || !pass.target.empty(); });
    if (buffers)
        return "Uses multi-pass buffers, which aren't supported yet.";
    if (!shader.imported.empty())
        return "Uses imported images, which aren't supported yet.";
    return {};
}

auto FindInput(const IsfShader& shader, std::string_view name) -> const IsfInput*
{
    const auto it = std::find_if(shader.inputs.begin(), shader.inputs.end(), [&](const IsfInput& input) { return input.name == name; });
    return it == shader.inputs.end() ? nullptr : &*it;
}

auto IsfVertexShader() -> const char*
{
    return R"GLSL(#version 330 core
out vec2 isf_FragNormCoord;
void main()
{
    vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    isf_FragNormCoord = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
}

auto TranslateIsf(const IsfShader& shader) -> std::string
{
    std::string out = R"GLSL(#version 330 core
uniform float TIME;
uniform float TIMEDELTA;
uniform int FRAMEINDEX;
uniform vec2 RENDERSIZE;
uniform int PASSINDEX;
uniform vec4 DATE;
in vec2 isf_FragNormCoord;
out vec4 isf_FragColor;
)GLSL";
    for (const auto& input : shader.inputs)
        out += std::string("uniform ") + UniformType(input.type) + " " + input.name + ";\n";
    out += R"GLSL(vec4 IMG_NORM_PIXEL(sampler2D img, vec2 uv) { return texture(img, uv); }
vec4 IMG_PIXEL(sampler2D img, vec2 xy) { return texture(img, xy / vec2(textureSize(img, 0))); }
vec4 IMG_THIS_NORM_PIXEL(sampler2D img) { return texture(img, isf_FragNormCoord); }
vec4 IMG_THIS_PIXEL(sampler2D img) { return texture(img, isf_FragNormCoord); }
vec2 IMG_SIZE(sampler2D img) { return vec2(textureSize(img, 0)); }
)GLSL";
    // "#line n" numbers the line after it n on macOS's compiler (GLSL 3.30's spec text says n + 1; drivers
    // follow the later specs). test_isf_program checks a compiler message lands on the file's line.
    out += "#line " + std::to_string(shader.glslLine) + "\n";

    std::string body = shader.glsl;
    ReplaceWord(body, "gl_FragColor", "isf_FragColor");
    ReplaceWord(body, "texture2D", "texture");
    ReplaceWord(body, "texture2DLod", "textureLod");
    ReplaceWord(body, "texture2DProj", "textureProj");
    ReplaceWord(body, "vv_FragNormCoord", "isf_FragNormCoord");
    ReplaceWord(body, "varying", "in");
    return out + body;
}

} // namespace gv::isf
