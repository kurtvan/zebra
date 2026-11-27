#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gv::model {

constexpr float kMaxContrast = 100.0f; //!< At ~50-100 the video becomes a hard split at mid-grey.
constexpr int kMaxPlaylistCount = 256; //!< Switcher inputs, and beat/bar interval counts.
constexpr double kMinPlaylistInterval = 1.0;
constexpr double kMaxPlaylistInterval = 3600.0;
constexpr int kMaxGraphNodes = 256;

enum class VideoSourceType
{
    File,
    Syphon
};

struct VideoSource
{
    VideoSourceType type{VideoSourceType::File};
    std::string videoId;        //!< File sources: the pool video (Performance::videos).
    std::filesystem::path path; //!< Format 1 only (a file path per scene); turned into a pool video when loading.
    std::string appName;        //!< Syphon sources: matched together with serverName (server UUIDs change per launch).
    std::string serverName;     //!< An empty name matches any; both empty connects to the first server found.

    auto operator==(const VideoSource&) const -> bool = default;
};

/// What a switcher's (or old playlist's) interval counts.
enum class IntervalUnit
{
    Seconds,
    Beats, //!< MIDI clock beats.
    Bars,  //!< MIDI clock bars (4/4).
};

// --- Nodes ---------------------------------------------------------------------------------------

enum class NodeType
{
    VideoSource,
    Contrast,
    Saturation,
    LumaGate,
    Opacity,
    MilkdropPreset,
    Switcher,
    Viewport,
    IsfShader,
    Lfo,
    AudioIn,
    Envelope,
    Band
};
constexpr int kNodeTypeCount = 13; //!< Every NodeType has a definition (model/nodes/NodeDef.hpp).

/// What travels along a wire: a picture (a video's, an effect's, or a preset's finished frame), a number
/// (0..1 this frame, an LFO's, say) that moves a setting, or audio (the Setup panel's input, or one range of
/// it from a Band, for an Envelope to follow). A wire joins two ports of the same kind; what the engine can't render yet is refused by the
/// picture rules in ValidateGraph.
enum class PortKind
{
    Picture,
    Number,
    Audio
};

/// The video inputs of a preset node (Wire::toInput).
enum class PresetLayer
{
    Feedback = 0,        //!< Into the previous frame, before the preset's warp.
    BeforeComposite = 1, //!< After warp, waves and shapes; before the composite shader.
    OnTop = 2,           //!< Over the finished frame.
    Texture = 3,         //!< In place of one of the preset's textures (Wire::textureName).
};
constexpr int kPresetInputs = 4;

struct VideoSourceParams
{
    VideoSource source;
    bool restartOnEnter{true};
    bool loop{true}; //!< File sources. Off: plays to the end once and holds its last frame.
    auto operator==(const VideoSourceParams&) const -> bool = default;
};
struct ContrastParams
{
    float contrast{1.0f}; //!< 0..kMaxContrast, 1 = unchanged.
    auto operator==(const ContrastParams&) const -> bool = default;
};
struct SaturationParams
{
    float saturation{1.0f}; //!< 0..2, 1 = unchanged, 0 = greyscale.
    auto operator==(const SaturationParams&) const -> bool = default;
};
struct LumaGateParams
{
    float threshold{0.0f}; //!< 0..1: pixels darker than this are cut (brighter ones when inverted).
    float softness{0.0f};  //!< 0..1: the edge's width, 0 = hard.
    bool invert{false};
    auto operator==(const LumaGateParams&) const -> bool = default;
};
struct OpacityParams
{
    float opacity{1.0f}; //!< 0..1
    auto operator==(const OpacityParams&) const -> bool = default;
};
struct MilkdropPresetParams
{
    std::filesystem::path path;
    /// The library folder ◀ ▶ step through, with its subfolders: "Category", "Category/Subcategory", or "" for
    /// the whole library. Nullopt: the preset's own folder (not its subfolders).
    std::optional<std::string> folder;
    auto operator==(const MilkdropPresetParams&) const -> bool = default;
};
struct SwitcherParams
{
    IntervalUnit unit{IntervalUnit::Seconds};
    double intervalSeconds{30.0}; //!< Unit Seconds: kMinPlaylistInterval..kMaxPlaylistInterval.
    int intervalCount{4};         //!< Units Beats/Bars: 1..kMaxPlaylistCount, changing on bar lines.
    bool shuffle{false};          //!< Random order, never the same input twice in a row.
    int inputCount{1};            //!< Always ends with one empty input (FollowWires).
    auto operator==(const SwitcherParams&) const -> bool = default;
};
struct ViewportParams
{
    auto operator==(const ViewportParams&) const -> bool = default;
};
struct IsfShaderParams
{
    std::filesystem::path path; //!< The .fs file; its header gives the node's inputs and controls.
    /// The controls' values, by ISF input name (1 number for float, bool and long). Values for inputs the file
    /// no longer has are kept but unused.
    std::map<std::string, std::vector<float>> values;
    /// Not saved: 1 + the highest wired input (FollowWires), so a missing or edited file keeps its wires.
    int wiredInputs{0};
    auto operator==(const IsfShaderParams&) const -> bool = default;
};
enum class LfoShape
{
    Sine,
    Triangle,
    Saw,
    Square
};
constexpr float kLfoBeats[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f}; //!< The cycle lengths an LFO offers, in beats.
struct LfoParams
{
    LfoShape shape{LfoShape::Sine};
    float beats{1.0f};         //!< One of kLfoBeats: the cycle's length while the MIDI clock runs.
    float phase{0.0f};         //!< 0..1: how far into its cycle the LFO starts.
    double periodSeconds{2.0}; //!< 0.1..60: the cycle's length without a running clock.
    auto operator==(const LfoParams&) const -> bool = default;
};

struct AudioInParams
{
    auto operator==(const AudioInParams&) const -> bool = default;
};
struct EnvelopeParams
{
    double attack{0.01};   //!< 0.001..1 s: how fast it rises when the audio gets louder.
    double release{0.25};  //!< 0.01..5 s: how fast it falls when the audio gets quieter.
    double gain{1.0};      //!< 0.1..20: without auto-level, the level is the audio's times this.
    double threshold{0.0}; //!< 0..0.99: levels below this are 0, the rest spread over 0..1.
    bool autoLevel{true};  //!< The loudest of the last few seconds is 1, so quiet and loud passages use 0..1.
    auto operator==(const EnvelopeParams&) const -> bool = default;
};
/// One frequency range of the audio: what a Band lets through (model/nodes/BandNode.hpp has its presets).
struct BandParams
{
    double low{20.0};   //!< 20..20000 Hz, at most `high`: below this is cut.
    double high{250.0}; //!< 20..20000 Hz: above this is cut.
    auto operator==(const BandParams&) const -> bool = default;
};

using NodeParams = std::variant<VideoSourceParams, ContrastParams, SaturationParams, LumaGateParams, OpacityParams, MilkdropPresetParams,
                                SwitcherParams, ViewportParams, IsfShaderParams, LfoParams, AudioInParams, EnvelopeParams,
                                BandParams>;

struct Node
{
    std::string id; //!< Unique within the scene's graph; never reused there.
    NodeType type{NodeType::Viewport};
    float x{0.0f}; //!< Canvas position.
    float y{0.0f};
    bool expanded{true}; //!< False: shown as its title bar only.
    NodeParams params{ViewportParams{}};
    /// How far each modulation input moves its setting, by ParamDef name: -1..1, missing is 1 (NodeDef::params).
    std::map<std::string, float> depths;

    auto operator==(const Node&) const -> bool = default;
};

struct Wire
{
    std::string fromNode; //!< Every node has at most one output.
    std::string toNode;
    int toInput{0};          //!< Preset: a PresetLayer. Switcher: the input index. Others: 0.
    std::string textureName; //!< Preset texture inputs only: the texture it replaces.
    /// Modulation inputs only: the param's name (ParamDef::name). The wire follows it: FollowWires moves
    /// `toInput` to that param's input when the node's params or inputs change, and drops the wire if the
    /// param is gone. Show files save the wire by this name.
    std::string param{};

    auto operator==(const Wire&) const -> bool = default;
};

struct Graph
{
    std::vector<Node> nodes;
    std::vector<Wire> wires;

    /// Same nodes in the same order, and the same wires in any order (saving sorts them).
    auto operator==(const Graph& other) const -> bool;

    auto Find(const std::string& id) const -> const Node*;
    auto Find(const std::string& id) -> Node*;
    auto Viewport() const -> const Node*;
    /// The wire into `node`'s input (`textureName` for preset texture inputs), if any.
    auto WireInto(const std::string& node, int input, const std::string& textureName = {}) const -> const Wire*;
    /// The node wired into `node`'s input, if any.
    auto Upstream(const std::string& node, int input, const std::string& textureName = {}) const -> const Node*;
};

/// The params of `node`, which must be of the matching type.
template <class Params>
auto ParamsOf(const Node& node) -> const Params&
{
    return std::get<Params>(node.params);
}
template <class Params>
auto ParamsOf(Node& node) -> Params&
{
    return std::get<Params>(node.params);
}

// Shorthands for the node type's definition (model/nodes/NodeDef.hpp), which has the rest.

/// Default params for a node type.
auto DefaultParams(NodeType type) -> NodeParams;
/// Short id prefix and display name, e.g. "gate" / "Luma gate".
auto IdPrefix(NodeType type) -> const char*;
auto DisplayName(NodeType type) -> const char*;

/// The kind a node outputs, or nullopt (the viewport has no output).
auto OutputKind(const Node& node) -> std::optional<PortKind>;
/// How many inputs a node has (preset: kPresetInputs, with texture inputs keyed by name), its modulation
/// inputs (one Number input per NodeDef::params entry, after the others) included.
auto InputCount(const Node& node) -> int;
/// The modulation input of `node`'s param `param` (an index into NodeDef::params).
auto ParamInput(const Node& node, int param) -> int;
/// The kind input `input` of `node` takes, or nullopt if out of range.
auto InputKind(const Node& node, int input) -> std::optional<PortKind>;

/// True for Contrast, Saturation, LumaGate and Opacity: one picture input, one picture output.
auto IsEffect(NodeType type) -> bool;

// --- Validation ----------------------------------------------------------------------------------

struct GraphError
{
    std::string nodeId; //!< The node the error is about; may be empty.
    std::string message; //!< Written for the artist.

    auto operator==(const GraphError&) const -> bool = default;
};

/// Every rule the graph breaks (technical doc §1, implementation plan §N1.4), each listed once. Unfinished
/// graphs (nothing wired into the viewport, loose nodes, effect chains without a source) are valid.
auto ValidateGraph(const Graph& graph) -> std::vector<GraphError>;

// --- Edits (pure; validation is the caller's job, see TryEditGraph) -------------------------------

/// A node id not used in `graph`, like "gate-3".
auto UniqueNodeId(const Graph& graph, NodeType type) -> std::string;
/// Adds a node with default params. Returns its id.
auto AddNode(Graph& graph, NodeType type, float x, float y) -> std::string;
/// Removes a node and its wires. Refuses the viewport. Returns false if refused or not found.
auto RemoveNode(Graph& graph, const std::string& id) -> bool;
/// Wires `from`'s output into `to`'s input, replacing a wire already there. Returns false if a node is missing.
auto Connect(Graph& graph, const std::string& from, const std::string& to, int input, const std::string& textureName = {}) -> bool;
/// Removes the wire into `to`'s input. Returns false if there was none.
auto Disconnect(Graph& graph, const std::string& to, int input, const std::string& textureName = {}) -> bool;
/// Updates every node's settings that follow its wires (NodeDef::followWires): a switcher keeps exactly one
/// empty input after its last wired one (inputs are never renumbered).
void FollowWires(Graph& graph);

/// What a node's picture is made from, for the rules the engine can't render yet: a video (sources, effects,
/// shaders), a preset, or both or neither (a switcher, from what's wired into it).
struct PictureFrom
{
    bool video{false};
    bool preset{false};
};
auto PictureOf(const Graph& graph, const Node& node) -> PictureFrom;
/// True for a switcher that switches videos: one with a video wired in or, with nothing wired in, one that
/// feeds something other than the viewport or a switcher.
auto SwitchesVideos(const Graph& graph, const Node& node) -> bool;
/// Every error wiring `from` into `to`'s input would cause (Connect on a copy, then FollowWires and
/// ValidateGraph), or none if the wire can be made. For an already valid graph, these are the wire's errors only.
auto ConnectErrors(const Graph& graph, const std::string& from, const std::string& to, int input, const std::string& textureName = {})
    -> std::vector<GraphError>;

// --- Queries used by the rest of the app ----------------------------------------------------------

/// The paths of every preset node, in node order.
auto PresetsOf(const Graph& graph) -> std::vector<std::filesystem::path>;
/// The pool video ids of every file video source node, in node order.
auto VideoIdsOf(const Graph& graph) -> std::vector<std::string>;

} // namespace gv::model
