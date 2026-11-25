#pragma once

#include "model/Graph.hpp"

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gv::model {

struct Issue;
struct Performance;

/// Something a show file's node is missing (NodeDef::checkInShow). An error stops the show from saving and
/// opening; a warning loads anyway and is listed in the issues.
struct ShowProblem
{
    bool error{false};
    std::string message; //!< Short, put after the node's id: "no video chosen".

    bool operator==(const ShowProblem&) const = default;
};

/// One shader pass of a video branch: what an effect node compiles to (engine/RenderPlan), and what the video
/// chain's shader runs (engine/VideoChain).
struct EffectPass
{
    NodeType type{NodeType::Contrast}; //!< An effect node type.
    float a{0.0f};                     //!< The shader's param.x.
    float b{0.0f};                     //!< The shader's param.y.
    bool flag{false};                  //!< The shader's flag.

    auto operator==(const EffectPass&) const -> bool = default;
};

/// Everything the model knows about one node type, in one place: model/nodes/<Type>Node.cpp. The graph
/// (ports, validation, edits), saving and loading, and the render plan all read it from here, so a new node
/// type is one file here (plus its canvas UI in app/nodes/ and its tests in tests/nodes/).
///
/// Function members may be null where noted; the free functions below give their defaults.
struct NodeDef
{
    NodeType type{NodeType::Viewport};
    const char* jsonName{""};    //!< "lumaGate": the node's "type" in show files.
    /// Earlier jsonNames, still read so older show files open ("mix" for Opacity). Renaming a type moves
    /// its old jsonName here; the next save writes the new one.
    std::span<const char* const> formerJsonNames{};
    const char* idPrefix{""};    //!< "gate": new nodes are "gate-1", "gate-2"...
    const char* displayName{""}; //!< "Luma gate"
    NodeParams defaults;         //!< A new node's settings (and the variant alternative it must hold).
    bool effect{false};          //!< One video input, one video output; compiles to an EffectPass.

    // Ports.
    std::optional<PortKind> (*output)(const Node&){nullptr}; //!< Null: no output.
    int (*inputCount)(const Node&){nullptr};                  //!< Null: no inputs.
    PortKind (*inputKind)(const Node&, int input){nullptr};   //!< For 0 <= input < inputCount.
    int textureInput{-1}; //!< The input whose wires are keyed by texture name (Wire::textureName); -1: none.
    std::span<const char* const> inputNames; //!< Names for inputs in show files, by index; empty: numbers.
    /// Settings that follow the node's wires (a switcher's input count). FollowWires runs it after
    /// every wire change and on load, so the node's settings and its wires never disagree.
    void (*followWires)(const Graph&, Node&){nullptr};

    /// "Video file": how errors and the canvas name this node. Null: displayName.
    std::string (*label)(const Node&){nullptr};

    // Validation (ValidateGraph). Messages are written for the artist and start with the node's Label().
    void (*checkParams)(const Node&, std::vector<GraphError>&){nullptr}; //!< The node's own settings.
    /// Rules about the node's place in the graph. Run only on graphs with no loops and only on wires whose
    /// ends exist.
    void (*checkInGraph)(const Graph&, const Node&, std::vector<GraphError>&){nullptr};

    /// Rules for a show file (Validate): what the node needs from the rest of the performance. Something
    /// not chosen yet is a warning, so a half-built scene still saves and comes back from crash recovery.
    void (*checkInShow)(const Performance&, const Node&, std::vector<ShowProblem>& problems){nullptr};

    // Show files: the node's settings, next to its id, type, position and expanded flag.
    void (*write)(const Node&, nlohmann::json& object){nullptr};
    void (*read)(const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues){nullptr};

    // Rendering (effects only).
    EffectPass (*pass)(const Node&){nullptr};
    /// GLSL run for each pass of this type, inside the video chain shader's loop. It may change `color`
    /// (vec3, not clamped between passes) and `alpha` (float), and reads `param` (vec2: a, b), `flag` (bool)
    /// and the constant `lumaWeights`.
    const char* shader{nullptr};
};

/// The definition of `type`.
auto Def(NodeType type) -> const NodeDef&;
/// Every node type's definition, in NodeType order.
auto NodeDefs() -> std::span<const NodeDef>;
/// The type whose jsonName, or one of its formerJsonNames, is `name`.
auto NodeTypeFromJson(std::string_view name) -> std::optional<NodeType>;

/// How errors and the canvas name `node` ("Video file").
auto Label(const Node& node) -> std::string;

/// Records "<Label>: <what> must be between <range>." unless low <= value <= high.
void CheckRange(std::vector<GraphError>& errors, const Node& node, const char* what, double value, double low, double high,
                const char* range);

/// Walks upstream from `node` through effect nodes (and ISF shaders' first input) to the node the chain starts from (a video source or
/// a Video switcher). Null if the chain has no start yet or loops.
auto ChainStart(const Graph& graph, const Node* node) -> const Node*;

namespace nodes {

// One per node type, in model/nodes/. NodeDefs() collects them.
auto VideoSourceDef() -> NodeDef;
auto ContrastDef() -> NodeDef;
auto SaturationDef() -> NodeDef;
auto LumaGateDef() -> NodeDef;
auto OpacityDef() -> NodeDef;
auto MilkdropPresetDef() -> NodeDef;
auto SwitcherDef() -> NodeDef;
auto ViewportDef() -> NodeDef;
auto IsfShaderDef() -> NodeDef;

/// The parts every effect shares: one video input, one video output.
auto EffectBase(NodeType type, const char* jsonName, const char* idPrefix, const char* displayName, NodeParams defaults) -> NodeDef;

} // namespace nodes

} // namespace gv::model
