// The ISF shader node: model/nodes/IsfShaderNode.cpp, app/nodes/IsfShaderNode.cpp. The parser: tests/isf/.

#include "nodes/NodeTestKit.hpp"

#include "engine/RenderPlan.hpp"
#include "model/nodes/IsfShaderNode.hpp"

#include <unistd.h>

#include <fstream>

using namespace gv;
using model::IsfShaderParams;
using model::NodeType;

namespace {

const std::filesystem::path kInvert = std::filesystem::path(GV_TEST_FIXTURES_DIR) / "isf" / "invert.fs";

/// A folder of this process's own (ctest runs test cases in parallel), emptied first.
auto TempDir() -> std::filesystem::path
{
    const auto dir = std::filesystem::temp_directory_path() / ("gv_test_isf_shader_" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

/// Writes an ISF file with `inputs` (the INPUTS list's JSON) and moves its time on, so the cache sees a change.
void WriteShader(const std::filesystem::path& file, const std::string& inputs)
{
    const bool existed = std::filesystem::exists(file);
    const auto before = existed ? std::filesystem::last_write_time(file) : std::filesystem::file_time_type{};
    std::ofstream(file) << "/*{ \"INPUTS\": [" << inputs << "] }*/\nvoid main() { gl_FragColor = vec4(1.0); }\n";
    if (existed)
        std::filesystem::last_write_time(file, before + std::chrono::seconds(1));
}

auto Isf(const model::Node& node) -> const IsfShaderParams&
{
    return model::ParamsOf<IsfShaderParams>(node);
}

auto WithPath(const std::filesystem::path& path) -> std::function<void(model::Node&)>
{
    return [=](model::Node& node) { model::ParamsOf<IsfShaderParams>(node).path = path; };
}

/// The node's controls of `kind`.
auto Controls(const model::Node& node, app::NodeContext& context, app::Control::Kind kind) -> std::vector<app::Control>
{
    std::vector<app::Control> found;
    for (auto& control : app::NodeControls(node, context))
    {
        if (control.kind == kind)
            found.push_back(std::move(control));
    }
    return found;
}

auto Problems(const test::NodeScene& scene, const model::Node& node) -> std::vector<model::ShowProblem>
{
    std::vector<model::ShowProblem> problems;
    model::Def(NodeType::IsfShader).checkInShow(scene.performance, node, problems);
    return problems;
}

} // namespace

TEST_CASE("ISF shader: a video output, and no inputs until a file is chosen", "[node:isf-shader]")
{
    const auto& def = model::Def(NodeType::IsfShader);
    CHECK_FALSE(def.effect);
    CHECK(std::string(def.jsonName) == "isfShader");
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::IsfShader));
    CHECK(node.id == "isf-1");
    CHECK(Isf(node) == IsfShaderParams{});
    CHECK(model::OutputKind(node) == model::PortKind::Picture);
    CHECK(model::InputCount(node) == 0);
}

TEST_CASE("ISF shader: a Video input per image input, following the file as it changes", "[node:isf-shader]")
{
    const auto file = TempDir() / "mix.fs";
    WriteShader(file, R"({"NAME": "inputImage", "TYPE": "image"}, {"NAME": "other", "LABEL": "Other", "TYPE": "image"},
                         {"NAME": "amount", "TYPE": "float"})");
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::IsfShader, WithPath(file));
    test::FakeNodeContext context(scene);
    CHECK(model::InputCount(scene.Node(id)) == 3); // and amount's modulation input
    auto inputs = app::NodeInputs(scene.Node(id), context);
    REQUIRE(inputs.size() == 2);
    CHECK(inputs[0].label == "inputImage");
    CHECK(inputs[1].label == "Other");
    CHECK(model::InputKind(scene.Node(id), 1) == model::PortKind::Picture);

    WriteShader(file, R"({"NAME": "inputImage", "TYPE": "image"})");
    CHECK(model::InputCount(scene.Node(id)) == 1);
    CHECK(app::NodeTitle(scene.Node(id), context) == "mix");

    scene.Node(id).params = IsfShaderParams{kInvert, {}};
    CHECK(model::ParamInput(scene.Node(id), 0) == 1); // one picture input, then Amount's
}

TEST_CASE("ISF shader: a missing or shrunk file keeps its wires, as inputs it no longer has", "[node:isf-shader]")
{
    const auto file = TempDir() / "two.fs";
    WriteShader(file, R"({"NAME": "inputImage", "TYPE": "image"}, {"NAME": "second", "TYPE": "image"})");
    test::NodeScene scene;
    const std::string video = scene.Add(NodeType::VideoSource);
    const std::string isf = scene.Add(NodeType::IsfShader, WithPath(file));
    REQUIRE(model::Connect(scene.Graph(), video, isf, 1));
    std::filesystem::remove(file);

    test::FakeNodeContext context(scene);
    CHECK(model::InputCount(scene.Node(isf)) == 2);
    const auto inputs = app::NodeInputs(scene.Node(isf), context);
    REQUIRE(inputs.size() == 2);
    CHECK(inputs[1].label == "Input 2 (not in the shader)");
    CHECK(model::ValidateGraph(scene.Graph()).empty());

    // A show file with the file missing loads with a warning, wires and all.
    const auto loaded = model::ParsePerformance(model::SerializePerformance(scene.performance));
    CHECK(loaded.ok);
    REQUIRE(loaded.performance.scenes.size() == 1);
    CHECK(loaded.performance.scenes[0].graph.wires == scene.Graph().wires);
}

TEST_CASE("ISF shader: saved with its path, values (including for inputs the file doesn't have) and Resolution", "[node:isf-shader]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::IsfShader, [](model::Node& n) {
        model::ParamsOf<IsfShaderParams>(n) = {
            kInvert, {{"amount", {0.25f}}, {"gone", {3.f}}, {"tint", {1.f, 0.5f, 0.f, 1.f}}}, model::IsfResolution::Quarter};
    }));
    CHECK(test::RoundTrip(node) == node);

    auto [read, issues] = test::ReadNode(NodeType::IsfShader, {{"path", "a.fs"}, {"values", {{"on", true}, {"radius", 4}}}});
    CHECK(Isf(read).path == "a.fs");
    CHECK(Isf(read).values == std::map<std::string, std::vector<float>>{{"on", {1.f}}, {"radius", {4.f}}});
    CHECK(issues.empty());
    CHECK(test::ReadNode(NodeType::IsfShader, {{"values", {{"radius", "big"}}}}).second.size() == 1);
    CHECK(test::ReadNode(NodeType::IsfShader, {{"values", 3}}).second.size() == 1);
    CHECK(Isf(read).resolution == model::IsfResolution::Full); // older shows have none
    CHECK(Isf(test::ReadNode(NodeType::IsfShader, {{"resolution", "half"}}).first).resolution == model::IsfResolution::Half);
    CHECK(test::ReadNode(NodeType::IsfShader, {{"resolution", "tiny"}}).second.size() == 1);
    CHECK(model::IsfDivisor(model::IsfResolution::Full) == 1);
    CHECK(model::IsfDivisor(model::IsfResolution::Half) == 2);
    CHECK(model::IsfDivisor(model::IsfResolution::Quarter) == 4);
}

TEST_CASE("ISF shader: Resolution is a choice on the node that its step renders at", "[node:isf-shader]")
{
    const auto file = TempDir() / "buffers.fs";
    std::ofstream(file) << R"(/*{ "INPUTS": [ {"NAME": "inputImage", "TYPE": "image"} ], "PASSES": [ { "TARGET": "trail", "PERSISTENT": true }, {} ] }*/
void main() { gl_FragColor = IMG_THIS_PIXEL(trail); }
)";
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::MilkdropPreset);
    const std::string video = scene.Add(NodeType::VideoSource);
    const std::string isf = scene.Add(NodeType::IsfShader, WithPath(file));
    model::Connect(scene.Graph(), preset, "viewport", 0);
    model::Connect(scene.Graph(), video, isf, 0);
    model::Connect(scene.Graph(), isf, preset, static_cast<int>(model::PresetLayer::Feedback));
    test::FakeNodeContext context(scene);

    const auto choices = Controls(scene.Node(isf), context, app::Control::Kind::Choice);
    REQUIRE_FALSE(choices.empty());
    const auto& resolution = choices[0];
    REQUIRE(resolution.options.size() == 3);
    CHECK(std::string(resolution.options[0]) == "Full");
    CHECK(resolution.get(scene.Node(isf)) == 0.0);
    resolution.set(scene.Node(isf), 1.0);
    CHECK(Isf(scene.Node(isf)).resolution == model::IsfResolution::Half);
    const auto plan = engine::CompilePlan(scene.Graph());
    REQUIRE(plan.branches.size() == 1);
    REQUIRE(plan.branches[0].steps.size() == 1);
    CHECK(plan.branches[0].steps[0].isf->divisor == 2);
}

TEST_CASE("ISF shader: no file, a missing file and a broken file are warnings, not errors", "[node:isf-shader]")
{
    const auto broken = TempDir() / "broken.fs";
    std::ofstream(broken) << "/*{ \"INPUTS\": [ }*/\nvoid main() {}\n";
    test::NodeScene scene;
    const std::string none = scene.Add(NodeType::IsfShader);
    const std::string missing = scene.Add(NodeType::IsfShader, WithPath("/nowhere/blur.fs"));
    const std::string bad = scene.Add(NodeType::IsfShader, WithPath(broken));
    CHECK(Problems(scene, scene.Node(none)) == std::vector<model::ShowProblem>{{false, "no shader file chosen"}});
    CHECK(Problems(scene, scene.Node(missing)) == std::vector<model::ShowProblem>{{false, "shader file not found: /nowhere/blur.fs"}});
    const auto problems = Problems(scene, scene.Node(bad));
    REQUIRE(problems.size() == 1);
    CHECK_FALSE(problems[0].error);
    CHECK(problems[0].message.starts_with("the shader file can't be read: line 1: "));
    CHECK(Problems(scene, scene.Node(scene.Add(NodeType::IsfShader, WithPath(kInvert)))).empty());

    test::FakeNodeContext context(scene);
    CHECK(app::NodeStatusOf(scene.Node(missing), context).state == app::NodeState::Missing);
    CHECK(app::NodeStatusOf(scene.Node(bad), context).tooltip.starts_with("Can't read the shader: line 1: "));
    CHECK(model::ValidateGraph(scene.Graph()).empty());
}

TEST_CASE("ISF shader: a value outside the file's MIN and MAX is an error; values it doesn't use aren't checked", "[node:isf-shader]")
{
    auto values = [](std::map<std::string, std::vector<float>> v) {
        return [=](model::Node& node) { model::ParamsOf<IsfShaderParams>(node) = {kInvert, v}; };
    };
    CHECK(test::ParamErrors(NodeType::IsfShader, values({{"amount", {1.f}}, {"gone", {99.f}}})).empty());
    CHECK(test::ParamErrors(NodeType::IsfShader, values({{"amount", {1.5f}}})) ==
          std::vector<std::string>{"ISF shader: Amount must be between 0 and 1."});
}

TEST_CASE("ISF shader: on the canvas, a slider per float, a checkbox per bool and a choice per long", "[node:isf-shader]")
{
    const auto file = TempDir() / "controls.fs";
    WriteShader(file, R"({"NAME": "inputImage", "TYPE": "image"},
        {"NAME": "radius", "LABEL": "Radius", "TYPE": "float", "MIN": 0.0, "MAX": 32.0, "DEFAULT": 4.0},
        {"NAME": "angle", "TYPE": "float"},
        {"NAME": "mirror", "LABEL": "Mirror", "TYPE": "bool"},
        {"NAME": "mode", "LABEL": "Mode", "TYPE": "long", "VALUES": [2, 5], "LABELS": ["Soft", "Hard"], "DEFAULT": 5},
        {"NAME": "tint", "TYPE": "color"})");
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::IsfShader, WithPath(file));
    test::FakeNodeContext context(scene);

    const auto sliders = Controls(scene.Node(id), context, app::Control::Kind::Slider);
    REQUIRE(sliders.size() == 2);
    CHECK(std::string(sliders[0].format) == "Radius %.2f");
    CHECK(sliders[0].min == 0.0);
    CHECK(sliders[0].max == 32.0);
    CHECK(sliders[0].get(scene.Node(id)) == 4.0); // its DEFAULT until set
    CHECK(std::string(sliders[0].field) == "radius");
    CHECK(std::string(sliders[1].format) == "angle %.2f");
    CHECK(sliders[1].max == 1.0);
    sliders[0].set(scene.Node(id), 10.0);
    CHECK(Isf(scene.Node(id)).values.at("radius") == std::vector<float>{10.f});

    const auto checkboxes = Controls(scene.Node(id), context, app::Control::Kind::Checkbox);
    REQUIRE(checkboxes.size() == 1);
    CHECK(std::string(checkboxes[0].label) == "Mirror");
    const auto choices = Controls(scene.Node(id), context, app::Control::Kind::Choice);
    REQUIRE(choices.size() == 2); // Resolution, then the long
    CHECK(std::string(choices[0].label) == "Resolution");
    REQUIRE(choices[1].options.size() == 2);
    CHECK(std::string(choices[1].options[1]) == "Hard");
    CHECK(choices[1].get(scene.Node(id)) == 1.0); // DEFAULT 5 is the second choice
    choices[1].set(scene.Node(id), 0.0);
    CHECK(Isf(scene.Node(id)).values.at("mode") == std::vector<float>{2.f});
    CHECK(model::ValidateGraph(scene.Graph()).empty());

    // Drawn headless: the image input's row, and no edits.
    test::HeadlessUi ui;
    ui.Frame(context, id);
    REQUIRE(context.inputRows.size() == 1);
    CHECK(context.inputRows[0].label == "inputImage");
    CHECK(context.edits.empty());
}

TEST_CASE("ISF shader: Choose... sets the file in one undo step, and Reveal shows it", "[node:isf-shader]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::IsfShader);
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    context.chosenFile = kInvert;
    ui.Frame(context, id);
    ui.Click(context, id, test::HeadlessUi::Centre(context, "An ISF filter (.fs file), as made for VDMX, Resolume or isf.video."));
    CHECK(context.fileDialogs == std::vector<std::string>{"Choose an ISF shader"});
    CHECK(context.LastEdit() == "Change ISF Shader");
    CHECK(Isf(scene.Node(id)).path == kInvert);
    ui.Frame(context, id); // the file's input row now sits above the buttons
    ui.Click(context, id, test::HeadlessUi::Centre(context, "Show the file in Finder."));
    CHECK(context.revealed == std::vector<std::filesystem::path>{kInvert});
}

TEST_CASE("ISF shader: compiles to an ISF step with its file and values", "[node:isf-shader]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::MilkdropPreset);
    const std::string video = scene.Add(NodeType::VideoSource);
    const std::string isf = scene.Add(NodeType::IsfShader, [](model::Node& n) { model::ParamsOf<IsfShaderParams>(n) = {kInvert, {{"amount", {1.f}}}}; });
    model::Connect(scene.Graph(), preset, "viewport", 0);
    model::Connect(scene.Graph(), video, isf, 0);
    model::Connect(scene.Graph(), isf, preset, static_cast<int>(model::PresetLayer::Feedback));
    CHECK(model::ChainStart(scene.Graph(), scene.Graph().Find(isf))->id == video);
    const auto plan = engine::CompilePlan(scene.Graph());
    REQUIRE(plan.branches.size() == 1);
    CHECK(plan.sourceNode == video);
    REQUIRE(plan.branches[0].steps.size() == 1);
    REQUIRE(plan.branches[0].steps[0].isf);
    CHECK(plan.branches[0].steps[0].isf->node == isf);
    CHECK(plan.branches[0].steps[0].isf->values.at("amount") == std::vector<float>{1.f});
}

TEST_CASE("ISF shader: a shader that doesn't compile is an error on the node, with the compiler's message", "[node:isf-shader]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::IsfShader, WithPath(kInvert));
    test::FakeNodeContext context(scene);
    context.shaderErrors[kInvert.string()] = "ERROR: 0:14: 'colour' : undeclared identifier\nERROR: 1 compilation error";
    const auto status = app::NodeStatusOf(scene.Node(id), context);
    CHECK(status.state == app::NodeState::Missing);
    CHECK(status.tooltip.ends_with("ERROR: 0:14: 'colour' : undeclared identifier\nERROR: 1 compilation error"));
    const auto texts = Controls(scene.Node(id), context, app::Control::Kind::Text);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0].text(scene.Node(id), context) == "Doesn't compile: ERROR: 0:14: 'colour' : undeclared identifier");
}

TEST_CASE("ISF shader: an edit the parser can't read keeps the last version, with the error on the node", "[node:isf-shader]")
{
    const auto file = TempDir() / "edited.fs";
    WriteShader(file, R"({"NAME": "inputImage", "TYPE": "image"}, {"NAME": "amount", "TYPE": "float"})");
    const auto good = model::IsfFileAt(file).shader;
    REQUIRE(good != nullptr);

    const auto before = std::filesystem::last_write_time(file);
    std::ofstream(file) << "/*{ \"INPUTS\": [ }*/\nvoid main() {}\n";
    std::filesystem::last_write_time(file, before + std::chrono::seconds(1));
    const auto broken = model::IsfFileAt(file);
    CHECK(broken.shader == good);
    CHECK(broken.error.starts_with("line 1: "));

    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::IsfShader, WithPath(file));
    test::FakeNodeContext context(scene);
    CHECK(model::InputCount(scene.Node(id)) == 2); // and amount's modulation input
    CHECK(app::NodeStatusOf(scene.Node(id), context).tooltip.ends_with("The last version that could be read keeps running."));

    WriteShader(file, R"({"NAME": "inputImage", "TYPE": "image"})");
    CHECK(model::IsfFileAt(file).shader != good);
    CHECK(model::IsfFileAt(file).error.empty());
}

TEST_CASE("ISF shader: a value set on an input an edit removes is kept, and comes back with the input", "[node:isf-shader]")
{
    const auto file = TempDir() / "comeback.fs";
    WriteShader(file, R"({"NAME": "amount", "TYPE": "float"})");
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::IsfShader, [&](model::Node& n) { model::ParamsOf<IsfShaderParams>(n) = {file, {{"amount", {0.75f}}}}; });
    test::FakeNodeContext context(scene);
    REQUIRE(Controls(scene.Node(id), context, app::Control::Kind::Slider).size() == 1);

    WriteShader(file, R"({"NAME": "other", "TYPE": "float"})");
    const auto sliders = Controls(scene.Node(id), context, app::Control::Kind::Slider);
    REQUIRE(sliders.size() == 1);
    CHECK(std::string(sliders[0].field) == "other");
    CHECK(test::RoundTrip(scene.Node(id)) == scene.Node(id)); // saved, though hidden

    WriteShader(file, R"({"NAME": "amount", "TYPE": "float"})");
    const auto back = Controls(scene.Node(id), context, app::Control::Kind::Slider);
    REQUIRE(back.size() == 1);
    CHECK(back[0].get(scene.Node(id)) == 0.75);
}

TEST_CASE("ISF shader: a feature that can't run yet is named on the node, and the shader isn't compiled", "[node:isf-shader]")
{
    const auto dir = TempDir();
    const std::pair<const char*, const char*> cases[] = {
        {R"x("PASSES": [ { "TARGET": "bufferA", "WIDTH": "floor($WIDTH)" }, {} ])x", "A pass has a size that can't be worked out: \"floor($WIDTH)\"."},
        {R"("IMPORTED": { "noise": { "PATH": "noise.png" } })", "Uses imported images, which aren't supported yet."},
    };
    for (const auto& [header, message] : cases)
    {
        INFO(header);
        const auto file = dir / (std::string(header).substr(1, 6) + ".fs"); // a file per case: the cache keys on time
        std::ofstream(file) << "/*{ \"INPUTS\": [ {\"NAME\": \"inputImage\", \"TYPE\": \"image\"} ], " << header
                            << " }*/\nvoid main() { gl_FragColor = IMG_THIS_PIXEL(bufferA); }\n";
        test::NodeScene scene;
        const std::string preset = scene.Add(NodeType::MilkdropPreset);
        const std::string video = scene.Add(NodeType::VideoSource);
        const std::string isf = scene.Add(NodeType::IsfShader, WithPath(file));
        model::Connect(scene.Graph(), preset, "viewport", 0);
        model::Connect(scene.Graph(), video, isf, 0);
        model::Connect(scene.Graph(), isf, preset, static_cast<int>(model::PresetLayer::Feedback));

        CHECK(model::IsfFileAt(file).unsupported == message);
        CHECK(model::InputCount(scene.Node(isf)) == 1); // its inputs and controls still show
        test::FakeNodeContext context(scene);
        const auto texts = Controls(scene.Node(isf), context, app::Control::Kind::Text);
        REQUIRE(texts.size() == 1);
        CHECK(texts[0].text(scene.Node(isf), context) == message);
        const auto status = app::NodeStatusOf(scene.Node(isf), context);
        CHECK(status.state == app::NodeState::Missing);
        CHECK(status.tooltip == std::string(message) + " The video passes through it.");
        CHECK(Problems(scene, scene.Node(isf)) == std::vector<model::ShowProblem>{{false, std::string("the shader can't run: ") + message}});

        // Left out of the plan, so the engine never compiles it and the video passes through.
        const auto plan = engine::CompilePlan(scene.Graph());
        REQUIRE(plan.branches.size() == 1);
        CHECK(plan.sourceNode == video);
        CHECK(plan.branches[0].steps.empty());
    }
}

TEST_CASE("ISF shader: a long error wraps inside the node's width", "[node:isf-shader]")
{
    const auto file = TempDir() / "wraps.fs";
    std::ofstream(file) << "/*{ }*/\nvoid main() { gl_FragColor = vec4(1.0); }\n";
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::IsfShader, WithPath(file));
    test::FakeNodeContext context(scene);
    context.shaderErrors[file.string()] =
        "ERROR: 0:354: Use of undeclared identifier 'bufferB_positionAndMass', which is a long way past the edge of any node\nmore";
    test::HeadlessUi ui;
    ui.Frame(context, id);

    // The text is the body's widest item, and the last: nothing reaches past the node's width.
    const ImGuiWindow* window = ImGui::FindWindowByName("node");
    REQUIRE(window != nullptr);
    CHECK(window->DC.CursorMaxPos.x - window->DC.CursorStartPos.x <= context.width + 0.5f);
    CHECK(window->DC.CursorMaxPos.y - window->DC.CursorStartPos.y > 4.f * ImGui::GetTextLineHeight()); // on several lines
    // The full message is in the status tooltip.
    CHECK(app::NodeStatusOf(scene.Node(id), context).tooltip.ends_with("of any node\nmore"));
}

// Modulation (D10, #77): the file's float, bool and long inputs are params, with pins, depths and wires.

namespace {

constexpr const char* kModulated = R"({"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "radius", "LABEL": "Radius", "TYPE": "float", "MIN": 0.0, "MAX": 32.0, "DEFAULT": 4.0},
    {"NAME": "mirror", "LABEL": "Mirror", "TYPE": "bool"},
    {"NAME": "mode", "LABEL": "Mode", "TYPE": "long", "VALUES": [2, 5, 9], "LABELS": ["Soft", "Hard", "Max"], "DEFAULT": 2},
    {"NAME": "tint", "TYPE": "color"}, {"NAME": "centre", "TYPE": "point2D"}, {"NAME": "flash", "TYPE": "event"})";

/// The value `param` of `node` takes from `signal` through a wire, at `depth`.
auto Moved(model::Node node, const std::string& param, float signal, float depth = 1.f) -> std::vector<float>
{
    node.depths[param] = depth;
    const int index = model::ParamIndex(node, param);
    REQUIRE(index >= 0);
    const std::vector<std::pair<int, std::string>> wired{{index, "lfo"}};
    return model::ParamsOf<IsfShaderParams>(model::ModulatedNode(node, wired, {{"lfo", signal}})).values.at(param);
}

} // namespace

TEST_CASE("ISF shader: each float, bool and long has a modulation pin starting its row; colour, point and event don't", "[node:isf-shader]")
{
    const auto file = TempDir() / "pins.fs";
    WriteShader(file, kModulated);
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::IsfShader, WithPath(file));
    const auto params = model::ParamsFor(scene.Node(id));
    REQUIRE(params.size() == 3);
    CHECK(params[0].kind == model::ParamKind::Slider);
    CHECK(params[1].kind == model::ParamKind::Checkbox);
    CHECK(params[2].kind == model::ParamKind::Choice);
    CHECK(model::ParamInput(scene.Node(id), 0) == 1); // after its picture input
    CHECK(model::InputCount(scene.Node(id)) == 4);
    for (int param = 0; param < 3; ++param)
        CHECK(model::InputKind(scene.Node(id), model::ParamInput(scene.Node(id), param)) == model::PortKind::Number);

    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    REQUIRE(context.paramPins.size() == 3);
    CHECK(context.paramPins[0] == app::InputSpec{1, {}, model::PortKind::Number, "Radius"});
    CHECK(context.paramPins[1].label == "Mirror");
    CHECK(context.paramPins[2].label == "Mode");
    CHECK(context.edits.empty());

    // A wire in, and the checkbox and choice rows also show its depth.
    const std::string lfo = scene.Add(NodeType::Lfo);
    REQUIRE(model::Connect(scene.Graph(), lfo, id, model::ParamInput(scene.Node(id), 2)));
    CHECK(scene.Graph().wires.back().param == "mode");
    CHECK(model::ValidateGraph(scene.Graph()).empty());
    context.tooltips.clear();
    ui.Frame(context, id);
    CHECK(std::ranges::count(context.tooltips, std::string("How far the wire moves the setting either side of the slider (negative: the other way).")) == 1);
}

TEST_CASE("ISF shader: a wire moves a float by its depth, turns a bool on from the middle and snaps a long to its nearest choice", "[node:isf-shader]")
{
    const auto file = TempDir() / "moved.fs";
    WriteShader(file, kModulated);
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::IsfShader, WithPath(file)));

    CHECK(Moved(node, "radius", 1.f, 0.5f) == std::vector<float>{12.f}); // 4 + 0.5 * 0.5 * 32
    CHECK(Moved(node, "radius", 0.f) == std::vector<float>{0.f});        // clamped to MIN

    CHECK(Moved(node, "mirror", 0.5f) == std::vector<float>{0.f});
    CHECK(Moved(node, "mirror", 0.9f) == std::vector<float>{0.f}); // 0.4: still off
    CHECK(Moved(node, "mirror", 1.f) == std::vector<float>{1.f});  // 0.5: on

    // Choices 0..2 from "Soft" (2): the signal moves the choice, which picks the nearest VALUE.
    CHECK(Moved(node, "mode", 0.7f) == std::vector<float>{2.f});    // 0.4 -> Soft
    CHECK(Moved(node, "mode", 0.75f) == std::vector<float>{5.f});   // 0.5 -> Hard
    CHECK(Moved(node, "mode", 1.f) == std::vector<float>{5.f});     // 1 -> Hard
    CHECK(Moved(node, "mode", 1.f, 0.f) == std::vector<float>{2.f}); // depth 0: the choice as set
    model::Node hard = node;
    model::ParamsOf<IsfShaderParams>(hard).values["mode"] = {5.f};
    CHECK(Moved(hard, "mode", 1.f) == std::vector<float>{9.f});
    CHECK(Moved(hard, "mode", 0.f) == std::vector<float>{2.f});
}

TEST_CASE("ISF shader: its wired params reach its step's values every frame", "[node:isf-shader]")
{
    const auto file = TempDir() / "step.fs";
    WriteShader(file, kModulated);
    test::NodeScene scene;
    const std::string video = scene.Add(NodeType::VideoSource);
    const std::string isf = scene.Add(NodeType::IsfShader, WithPath(file));
    const std::string lfo = scene.Add(NodeType::Lfo);
    REQUIRE(model::Connect(scene.Graph(), video, isf, 0));
    REQUIRE(model::Connect(scene.Graph(), isf, "viewport", 0));
    REQUIRE(model::Connect(scene.Graph(), lfo, isf, model::ParamInput(scene.Node(isf), 0)));
    scene.Node(isf).depths["radius"] = 0.5f;
    REQUIRE(model::ValidateGraph(scene.Graph()).empty());

    const auto plan = engine::CompilePlan(scene.Graph());
    REQUIRE(plan.branches.size() == 1);
    const auto& steps = plan.branches[0].steps;
    REQUIRE(steps.size() == 1);
    REQUIRE(steps[0].isf);
    CHECK(engine::HasModulations(steps));
    const auto moved = engine::Modulated(steps, {{lfo, 1.f}});
    CHECK(moved[0].isf->values.at("radius") == std::vector<float>{12.f});
    CHECK(engine::Modulated(steps, {}).at(0).isf->values == steps[0].isf->values); // no value yet: the slider's
}

TEST_CASE("ISF shader: a wire into an input the file loses stays, saved and broken with a note, and works when it comes back", "[node:isf-shader]")
{
    const auto file = TempDir() / "lost.fs";
    WriteShader(file, R"({"NAME": "inputImage", "TYPE": "image"}, {"NAME": "speed", "TYPE": "float"}, {"NAME": "size", "TYPE": "float"})");
    test::NodeScene scene;
    const std::string isf = scene.Add(NodeType::IsfShader, WithPath(file));
    const std::string lfo = scene.Add(NodeType::Lfo);
    const std::string other = scene.Add(NodeType::Lfo);
    REQUIRE(model::Connect(scene.Graph(), lfo, isf, model::ParamInput(scene.Node(isf), 0)));
    scene.Node(isf).depths["speed"] = 0.25f;
    const model::Wire wire = scene.Graph().wires.back();
    CHECK(wire.param == "speed");

    // Renamed in the file: before any wire edit, the wire already finds its own pin, and "rate" isn't wired.
    WriteShader(file, R"({"NAME": "inputImage", "TYPE": "image"}, {"NAME": "rate", "TYPE": "float"}, {"NAME": "size", "TYPE": "float"})");
    REQUIRE(model::ParamsFor(scene.Node(isf)).size() == 3);
    CHECK(model::ParamsFor(scene.Node(isf))[2].lost);
    CHECK(std::string(model::ParamsFor(scene.Node(isf))[2].name) == "speed");
    CHECK(model::WireInput(scene.Graph(), wire) == model::ParamInput(scene.Node(isf), 2));
    CHECK(scene.Graph().Upstream(isf, model::ParamInput(scene.Node(isf), 0)) == nullptr);
    CHECK(scene.Graph().Upstream(isf, model::ParamInput(scene.Node(isf), 2))->id == lfo);

    // Another wire edit keeps it, and the node shows a note with its pin in place of a slider.
    REQUIRE(model::Connect(scene.Graph(), other, isf, model::ParamInput(scene.Node(isf), 1)));
    REQUIRE(scene.Graph().wires.size() == 2);
    CHECK(scene.Graph().wires[0].param == "speed");
    CHECK(model::ValidateGraph(scene.Graph()).empty());
    CHECK(model::ModulatedNode(scene.Graph(), scene.Node(isf), {{lfo, 1.f}}).params == scene.Node(isf).params); // it moves nothing
    test::FakeNodeContext context(scene);
    const auto notes = Controls(scene.Node(isf), context, app::Control::Kind::Text);
    REQUIRE(notes.size() == 2); // the file's problem line, then the lost input's
    CHECK(std::string(notes[1].label) == "'speed' isn't in this shader any more.");
    CHECK(notes[1].param == 2);
    test::HeadlessUi ui;
    ui.Frame(context, isf);
    CHECK(std::ranges::count(context.paramPins, app::InputSpec{model::ParamInput(scene.Node(isf), 2), {}, model::PortKind::Number, "speed"}) == 1);

    // Saved and loaded with no warning, and still there after the file goes missing.
    const auto loaded = model::ParsePerformance(model::SerializePerformance(scene.performance));
    CHECK(loaded.ok);
    CHECK(loaded.issues.size() == 1); // nothing on the viewport
    REQUIRE(loaded.performance.scenes.size() == 1);
    CHECK(loaded.performance.scenes[0].graph == scene.Graph());
    std::filesystem::remove(file);
    model::FollowWires(scene.Graph());
    CHECK(scene.Graph().wires.size() == 2);
    CHECK(model::ValidateGraph(scene.Graph()).empty());
    const auto missing = model::ParsePerformance(model::SerializePerformance(scene.performance));
    REQUIRE(missing.performance.scenes.size() == 1);
    CHECK(missing.performance.scenes[0].graph.wires.size() == 2);

    // Back in the file: the wire works again, with its depth.
    WriteShader(file, R"({"NAME": "inputImage", "TYPE": "image"}, {"NAME": "speed", "TYPE": "float"})");
    REQUIRE(model::ParamIndex(scene.Node(isf), "speed") == 0);
    CHECK_FALSE(model::ParamsFor(scene.Node(isf))[0].lost);
    CHECK(scene.Graph().Upstream(isf, model::ParamInput(scene.Node(isf), 0))->id == lfo);
    const auto moved = model::ModulatedNode(scene.Graph(), scene.Node(isf), {{lfo, 1.f}});
    CHECK(Isf(moved).values.at("speed") == std::vector<float>{0.125f}); // 0 + 0.5 * 0.25
}
