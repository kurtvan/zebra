#pragma once

// ISF (Interactive Shader Format) files: the JSON header, and the GLSL rewritten for our GL 3.30 core context.
// Pure: no GL, so the unit tests can run it. IsfProgram (src/isf/IsfProgram.hpp) compiles and draws the result.
//
// Coordinates and texture orientation:
// - Every texture in the app is bottom-up (row 0 is the bottom of the picture: VideoSource, RectToTexture and
//   every FBO we render into). ISF assumes the same GL convention: isf_FragNormCoord (0, 0) is the bottom-left
//   pixel, and IMG_NORM_PIXEL(img, vec2(0.0)) is the bottom-left texel of img.
// - So the IMG_* functions sample the texture as it is, with no flip and no per-image flip uniform (VVISF has
//   one because it accepts top-down textures; we never hand it one). A caller with a top-down texture must
//   flip it first, as RectToTexture does for CoreVideo frames.
// - The IMG_* macros are GLSL functions taking the sampler:
//   - IMG_NORM_PIXEL(img, uv): texture(img, uv), uv in 0..1.
//   - IMG_PIXEL(img, xy): xy in the image's own pixels (0..IMG_SIZE), divided by its size.
//   - IMG_THIS_PIXEL(img), IMG_THIS_NORM_PIXEL(img): img at isf_FragNormCoord, so an image of another size
//     is stretched to the render size.
//   - IMG_SIZE(img): textureSize, as a vec2.

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gv::isf {

/// An ISF input's "TYPE".
enum class InputType
{
    Float,
    Bool,
    Long,    //!< a choice: VALUES, shown as LABELS
    Color,   //!< RGBA
    Point2D,
    Event,   //!< a one-frame trigger
    Image,
    Audio,   //!< the host supplies a texture of samples
    AudioFft //!< the host supplies a texture of the spectrum
};

/// What a shader is, by the ISF convention on its image inputs.
enum class ShaderKind
{
    Filter,     //!< has an image input called inputImage
    Generator,  //!< has neither a filter's nor a transition's inputs
    Transition  //!< has image inputs startImage and endImage, and a float progress
};

/// One entry of the header's "INPUTS".
struct IsfInput
{
    std::string name;
    InputType type{InputType::Float};
    std::string label;                //!< "LABEL", or empty
    std::vector<float> defaultValue;  //!< "DEFAULT": 1 number (float, bool, long, event), 2 (point2D) or 4 (color); empty if none
    std::vector<float> min;           //!< "MIN", same sizes; empty if none
    std::vector<float> max;           //!< "MAX", same sizes; for audio and audioFFT, the sample count asked for
    std::vector<int> values;          //!< long: "VALUES" (0..n-1 if only LABELS are given)
    std::vector<std::string> labels;  //!< long: "LABELS"
};

/// One entry of the header's "PASSES". Stored as written; only the first pass is rendered for now.
struct IsfPass
{
    std::string target;  //!< "TARGET", or empty for the output
    bool persistent{false};
    bool floatTarget{false}; //!< "FLOAT"
    std::string width;   //!< "WIDTH" expression, e.g. "$WIDTH/2", or empty
    std::string height;  //!< "HEIGHT" expression, or empty
};

/// A parsed ISF file.
struct IsfShader
{
    std::string version;                 //!< "ISFVSN", or empty
    std::string description;             //!< "DESCRIPTION"
    std::string credit;                  //!< "CREDIT"
    std::vector<std::string> categories; //!< "CATEGORIES"
    ShaderKind kind{ShaderKind::Generator};
    std::vector<IsfInput> inputs;
    std::vector<IsfPass> passes;         //!< empty means one pass into the output
    std::string glsl;                    //!< the code after the header comment
    int glslLine{1};                     //!< the file line glsl starts on, so compiler messages name file lines
};

/// Why a file couldn't be read: the file's line (1-based) and a message for the artist.
struct IsfError
{
    int line{1};
    std::string message;
};

/// Reads an ISF file's header and splits off its GLSL.
auto ParseIsf(std::string_view source) -> std::variant<IsfShader, IsfError>;

/// The fragment shader for GL 3.30 core: generated uniforms for every input and the host values (TIME,
/// TIMEDELTA, FRAMEINDEX, RENDERSIZE, PASSINDEX, DATE), isf_FragNormCoord from IsfVertexShader(), the IMG_*
/// functions, then the shader's own code with gl_FragColor, texture2D and varying rewritten. A #line directive
/// keeps compiler messages on the file's own line numbers.
auto TranslateIsf(const IsfShader& shader) -> std::string;

/// The vertex shader every ISF program shares: a full-screen triangle that hands isf_FragNormCoord on.
auto IsfVertexShader() -> const char*;

/// The input called `name`, if the shader has one.
auto FindInput(const IsfShader& shader, std::string_view name) -> const IsfInput*;

} // namespace gv::isf
