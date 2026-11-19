#include "model/Document.hpp"

#include "model/PerformanceJson.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace gv::model {

namespace {
constexpr size_t kMaxUndoSteps = 500;
}

Document::Document(Performance performance, std::filesystem::path path)
    : m_current(std::move(performance))
    , m_path(std::move(path))
{
}

auto Document::Title() const -> std::string
{
    if (!m_current.name.empty())
        return m_current.name;
    if (!m_path.empty())
        return m_path.stem().string();
    return "Untitled";
}

void Document::Edit(Performance edited, std::string label, std::string coalesceKey)
{
    CheckFiles(edited);
    const bool coalesce = !coalesceKey.empty() && coalesceKey == m_coalesceKey && !m_undo.empty();
    if (!coalesce)
    {
        m_undo.push_back({std::move(m_current), m_currentId, std::move(label)});
        if (m_undo.size() > kMaxUndoSteps)
            m_undo.erase(m_undo.begin());
    }
    else
    {
        m_retired.push_back(std::move(m_current)); // the UI may still be reading it this frame
    }
    m_coalesceKey = std::move(coalesceKey);
    m_current = std::move(edited);
    m_currentId = m_nextId++;
    m_redo.clear();
    Changed();
}

auto Document::UndoLabel() const -> std::string
{
    return m_undo.empty() ? std::string() : m_undo.back().label;
}

auto Document::RedoLabel() const -> std::string
{
    return m_redo.empty() ? std::string() : m_redo.back().label;
}

void Document::Undo()
{
    if (m_undo.empty())
        return;
    Step step = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back({std::move(m_current), m_currentId, step.label});
    m_current = std::move(step.state);
    m_currentId = step.id;
    m_coalesceKey.clear();
    CheckFiles(m_current);
    Changed();
}

void Document::Redo()
{
    if (m_redo.empty())
        return;
    Step step = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back({std::move(m_current), m_currentId, step.label});
    m_current = std::move(step.state);
    m_currentId = step.id;
    m_coalesceKey.clear();
    CheckFiles(m_current);
    Changed();
}

void Document::MarkSaved(const std::filesystem::path& path)
{
    m_path = path;
    m_savedId = m_currentId;
    m_coalesceKey.clear();
}

void Document::Changed()
{
    m_revision++;
}

auto UniqueSceneId(const Performance& performance, const std::string& base) -> std::string
{
    std::set<std::string> ids;
    for (const auto& scene : performance.scenes)
        ids.insert(scene.id);
    for (int n = static_cast<int>(performance.scenes.size()) + 1;; ++n)
    {
        std::string candidate = base + "-" + std::to_string(n);
        if (ids.count(candidate) == 0)
            return candidate;
    }
}

auto TryEditGraph(Document& document, int scene, const std::function<void(Graph&)>& edit, std::string label, std::string coalesceKey)
    -> std::vector<GraphError>
{
    const auto& current = document.Get();
    if (scene < 0 || scene >= static_cast<int>(current.scenes.size()))
        return {{"", "That scene doesn't exist any more."}};
    Performance edited = current;
    auto& graph = edited.scenes[static_cast<size_t>(scene)].graph;
    edit(graph);
    NormalizeSwitcherInputs(graph);
    auto errors = ValidateGraph(graph);
    if (errors.empty())
        document.Edit(std::move(edited), std::move(label), std::move(coalesceKey));
    return errors;
}

namespace {

/// The preset nodes on the viewport's side, in switcher input order (one without a switcher).
auto ScenePresets(const Graph& graph) -> std::vector<std::string>
{
    const Node* viewport = graph.Viewport();
    const Node* top = viewport != nullptr ? graph.Upstream(viewport->id, 0) : nullptr;
    if (top == nullptr)
        return {};
    if (top->type == NodeType::Preset)
        return {top->id};
    std::vector<std::string> presets;
    if (top->type == NodeType::Switcher)
    {
        for (int i = 0; i < ParamsOf<SwitcherParams>(*top).inputCount; ++i)
        {
            if (const Node* input = graph.Upstream(top->id, i); input != nullptr && input->type == NodeType::Preset)
                presets.push_back(input->id);
        }
    }
    return presets;
}

} // namespace

auto ReplacePreset(Performance& performance, int index, int item, const std::filesystem::path& presetPath) -> int
{
    if (index < 0 || index >= static_cast<int>(performance.scenes.size()))
        return -1;
    auto& scene = performance.scenes[static_cast<size_t>(index)];
    auto& graph = scene.graph;
    const auto presets = ScenePresets(graph);
    if (presets.empty())
    {
        // Unfinished: a new preset node into the viewport.
        const Node* viewport = graph.Viewport();
        const std::string viewportId = viewport != nullptr ? viewport->id : AddNode(graph, NodeType::Viewport, 400.0f, 0.0f);
        const std::string preset = AddNode(graph, NodeType::Preset, 100.0f, 0.0f);
        ParamsOf<PresetParams>(*graph.Find(preset)).path = presetPath;
        Connect(graph, preset, viewportId, 0);
        scene.Refresh();
        return 0;
    }
    item = presets.size() > 1 ? std::clamp(item, 0, static_cast<int>(presets.size()) - 1) : 0;
    ParamsOf<PresetParams>(*graph.Find(presets[static_cast<size_t>(item)])).path = presetPath;
    scene.Refresh();
    return item;
}

auto AssignVideo(Performance& performance, int index, const std::string& videoId) -> bool
{
    if (index < 0 || index >= static_cast<int>(performance.scenes.size()))
        return false;
    auto& scene = performance.scenes[static_cast<size_t>(index)];
    auto& graph = scene.graph;
    const VideoSource file{VideoSourceType::File, videoId, {}, {}, {}};

    // A Video switcher: one more input. Otherwise the (first) video source plays the file.
    for (const auto& node : graph.nodes)
    {
        if (node.type == NodeType::Switcher && ParamsOf<SwitcherParams>(node).kind == PortKind::Video)
        {
            const std::string switcher = node.id;
            const int input = ParamsOf<SwitcherParams>(node).inputCount - 1;
            VideoSourceParams params;
            params.source = file;
            if (const Node* first = graph.Upstream(switcher, 0); first != nullptr && first->type == NodeType::VideoSource)
            {
                params.restartOnEnter = ParamsOf<VideoSourceParams>(*first).restartOnEnter;
                params.loop = ParamsOf<VideoSourceParams>(*first).loop;
            }
            const std::string source = AddNode(graph, NodeType::VideoSource, node.x - 280.0f, node.y + 230.0f * static_cast<float>(input));
            graph.Find(source)->params = params;
            Connect(graph, source, switcher, input);
            scene.Refresh();
            return true;
        }
    }
    for (auto& node : graph.nodes)
    {
        if (node.type == NodeType::VideoSource)
        {
            ParamsOf<VideoSourceParams>(node).source = file;
            scene.Refresh();
            return true;
        }
    }

    // No video yet: a source, and a default look chain into every preset's Feedback input.
    const std::string source = AddNode(graph, NodeType::VideoSource, 40.0f, 0.0f);
    ParamsOf<VideoSourceParams>(*graph.Find(source)).source = file;
    for (const auto& preset : ScenePresets(graph))
    {
        std::string previous = source;
        const float y = graph.Find(preset)->y; // the chain on the preset's row, ending left of it
        float x = graph.Find(preset)->x - 900.0f;
        for (NodeType type : {NodeType::Contrast, NodeType::Saturation, NodeType::LumaGate, NodeType::Mix})
        {
            const std::string id = AddNode(graph, type, x, y);
            if (type == NodeType::LumaGate)
                ParamsOf<LumaGateParams>(*graph.Find(id)).softness = kFeedbackGateSoftness;
            Connect(graph, previous, id, 0);
            previous = id;
            x += 220.0f;
        }
        Connect(graph, previous, preset, static_cast<int>(PresetLayer::Feedback));
    }
    scene.Refresh();
    return true;
}

auto AddScene(Performance& performance, int after, const std::filesystem::path& presetPath) -> int
{
    const auto cc = performance.FreeCc20Value();
    if (static_cast<int>(performance.scenes.size()) >= kMaxScenes || !cc)
        return -1;
    Scene scene;
    scene.id = UniqueSceneId(performance);
    ClassicScene classic;
    classic.presetPath = presetPath;
    scene.graph = GraphFromClassic(classic);
    scene.Refresh();
    scene.cc20Value = *cc;
    const int index = (after >= 0 && after < static_cast<int>(performance.scenes.size())) ? after + 1 : static_cast<int>(performance.scenes.size());
    performance.scenes.insert(performance.scenes.begin() + index, std::move(scene));
    return index;
}

auto DuplicateScene(Performance& performance, int index) -> int
{
    const auto cc = performance.FreeCc20Value();
    if (index < 0 || index >= static_cast<int>(performance.scenes.size()) || static_cast<int>(performance.scenes.size()) >= kMaxScenes || !cc)
        return -1;
    Scene copy = performance.scenes[static_cast<size_t>(index)];
    copy.id = UniqueSceneId(performance, copy.id);
    if (!copy.name.empty())
        copy.name += " copy";
    copy.cc20Value = *cc;
    performance.scenes.insert(performance.scenes.begin() + index + 1, std::move(copy));
    return index + 1;
}

auto DeleteScene(Performance& performance, int index) -> bool
{
    if (index < 0 || index >= static_cast<int>(performance.scenes.size()))
        return false;
    performance.scenes.erase(performance.scenes.begin() + index);
    return true;
}

auto MoveScene(Performance& performance, int from, int to) -> bool
{
    const int count = static_cast<int>(performance.scenes.size());
    if (from < 0 || from >= count || to < 0 || to >= count)
        return false;
    Scene scene = std::move(performance.scenes[static_cast<size_t>(from)]);
    performance.scenes.erase(performance.scenes.begin() + from);
    performance.scenes.insert(performance.scenes.begin() + to, std::move(scene));
    return true;
}

auto RenumberCc20(Performance& performance) -> int
{
    int changed = 0;
    for (size_t i = 0; i < performance.scenes.size(); ++i)
    {
        if (performance.scenes[i].cc20Value != static_cast<int>(i))
        {
            performance.scenes[i].cc20Value = static_cast<int>(i);
            changed++;
        }
    }
    return changed;
}

auto AddPoolVideo(Performance& performance, const std::filesystem::path& file) -> std::string
{
    for (const auto& video : performance.videos)
    {
        if (video.path == file)
            return video.id;
    }
    // A readable, unique id from the file name ("cave-in-prague", "cave-in-prague-2", ...).
    std::string base;
    for (char c : file.stem().string())
        base += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '-';
    if (base.empty())
        base = "video";
    std::string id = base;
    for (int n = 2; performance.FindVideo(id) != nullptr; ++n)
        id = base + "-" + std::to_string(n);
    PoolVideo video;
    video.id = id;
    video.path = file;
    performance.videos.push_back(std::move(video));
    return id;
}

auto VideoUsage(const Performance& performance, const std::string& id) -> int
{
    int count = 0;
    for (const auto& scene : performance.scenes)
    {
        if (scene.UsesVideo(id))
            count++;
    }
    return count;
}

auto RemovePoolVideo(Performance& performance, const std::string& id) -> bool
{
    if (VideoUsage(performance, id) > 0)
        return false;
    auto it = std::find_if(performance.videos.begin(), performance.videos.end(), [&](const PoolVideo& video) { return video.id == id; });
    if (it == performance.videos.end())
        return false;
    performance.videos.erase(it);
    return true;
}

} // namespace gv::model
