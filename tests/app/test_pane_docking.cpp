// How the Shell collapses an area: it stops submitting the area's windows. These tests pin down the ImGui docking
// behaviour that relies on: the empty dock node stays (invisible) with its size, its neighbours take the space, and
// the windows go back into the same node, at the same size and tab order, when they're submitted again, also after
// a restart through the .ini file.

#include <imgui.h>
#include <imgui_internal.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

constexpr const char* kScenes = "Scenes";
constexpr const char* kPreview = "Preview";
constexpr const char* kGraph = "Graph";
const std::vector<const char*> kBottom = {"Presets", "Videos", "Status", "Messages"};

class DockUi
{
public:
    explicit DockUi(const std::string& ini = {})
    {
        m_context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.DisplaySize = ImVec2(1600.f, 1000.f);
        io.IniFilename = nullptr;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        if (!ini.empty())
            ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
    }
    ~DockUi()
    {
        ImGui::DestroyContext(m_context);
    }
    DockUi(const DockUi&) = delete;
    auto operator=(const DockUi&) -> DockUi& = delete;

    /// A few frames, with or without the left (Scenes) and bottom areas' windows, like Shell::Draw.
    void Frames(bool left, bool bottom, int count = 3)
    {
        for (int i = 0; i < count; ++i)
        {
            ImGui::GetIO().DeltaTime = 1.f / 60.f;
            ImGui::NewFrame();
            const ImGuiID dockspace = ImGui::DockSpaceOverViewport();
            BuildLayoutOnce(dockspace);
            Window(kPreview);
            Window(kGraph);
            if (left)
                Window(kScenes);
            if (bottom)
                for (const char* name : kBottom)
                    Window(name);
            ImGui::Render();
        }
    }

    static auto Node(const char* window) -> ImGuiDockNode*
    {
        ImGuiWindow* found = ImGui::FindWindowByName(window);
        return found != nullptr ? found->DockNode : nullptr;
    }

    static auto SavedDockId(const char* window) -> ImGuiID
    {
        ImGuiWindow* found = ImGui::FindWindowByName(window);
        return found != nullptr ? found->DockId : 0;
    }

    static auto TabOrder(const char* window) -> std::vector<std::string>
    {
        std::vector<std::string> names;
        if (ImGuiDockNode* node = Node(window))
            for (ImGuiWindow* docked : node->Windows)
                names.emplace_back(docked->Name);
        return names;
    }

private:
    static void Window(const char* name)
    {
        ImGui::Begin(name);
        ImGui::Text("%s", name);
        ImGui::End();
    }

    /// Shell::BuildDefaultLayout's splits (without the right area), unless a saved layout was loaded.
    void BuildLayoutOnce(ImGuiID dockspace)
    {
        if (m_built)
            return;
        m_built = true;
        if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace); node != nullptr && !node->IsLeafNode())
            return;
        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
        ImGuiID center = dockspace;
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.27f, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.40f, nullptr, &center);
        const ImGuiID graph = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.55f, nullptr, &center);
        ImGui::DockBuilderDockWindow(kScenes, left);
        for (const char* name : kBottom)
            ImGui::DockBuilderDockWindow(name, bottom);
        ImGui::DockBuilderDockWindow(kPreview, center);
        ImGui::DockBuilderDockWindow(kGraph, graph);
        ImGui::DockBuilderFinish(dockspace);
    }

    ImGuiContext* m_context{nullptr};
    bool m_built{false};
};

/// The user drags the left area to `width`, the way the splitter does it.
void DragLeftTo(float width)
{
    ImGuiDockNode* left = DockUi::Node(kScenes);
    REQUIRE(left != nullptr);
    left->Size.x = left->SizeRef.x = width;
}

} // namespace

TEST_CASE("Pane docking: a collapsed area gives its space away and comes back at the same size")
{
    DockUi ui;
    ui.Frames(true, true);
    DragLeftTo(333.f);
    ui.Frames(true, true);
    ImGuiDockNode* left = DockUi::Node(kScenes);
    REQUIRE(left != nullptr);
    const ImGuiID leftId = left->ID;
    REQUIRE(left->Size.x == 333.f);
    const float previewWidth = DockUi::Node(kPreview)->Size.x;
    const float previewHeight = DockUi::Node(kPreview)->Size.y;

    // Collapse the left area: the preview widens to the window's edge.
    ui.Frames(false, true);
    CHECK(DockUi::Node(kScenes) == nullptr);
    CHECK(DockUi::SavedDockId(kScenes) == leftId);
    CHECK(DockUi::Node(kPreview)->Pos.x == 0.f);
    CHECK(DockUi::Node(kPreview)->Size.x > previewWidth + 300.f);

    // Collapse the bottom too: the preview and graph fill the window's height.
    ui.Frames(false, false);
    CHECK(DockUi::Node(kPreview)->Size.y > previewHeight);
    CHECK(DockUi::Node(kGraph)->Pos.y + DockUi::Node(kGraph)->Size.y == ImGui::GetMainViewport()->WorkSize.y);

    // Restore the left area: same node, same width.
    ui.Frames(true, false);
    REQUIRE(DockUi::Node(kScenes) != nullptr);
    CHECK(DockUi::Node(kScenes)->ID == leftId);
    CHECK(DockUi::Node(kScenes)->Size.x == 333.f);
    CHECK(DockUi::Node(kPreview)->Size.x == previewWidth);
}

TEST_CASE("Pane docking: a restored area keeps its tabs and their order")
{
    DockUi ui;
    ui.Frames(true, true);
    const auto tabs = DockUi::TabOrder("Presets");
    REQUIRE(tabs == std::vector<std::string>{"Presets", "Videos", "Status", "Messages"});
    const ImGuiID bottomId = DockUi::Node("Presets")->ID;

    ui.Frames(true, false);
    ui.Frames(true, true);
    CHECK(DockUi::Node("Presets")->ID == bottomId);
    CHECK(DockUi::TabOrder("Presets") == tabs);
}

TEST_CASE("Pane docking: an area collapsed at quit comes back at its size after a relaunch")
{
    std::string ini;
    ImGuiID leftId = 0;
    {
        DockUi ui;
        ui.Frames(true, true);
        DragLeftTo(333.f);
        ui.Frames(true, true);
        leftId = DockUi::Node(kScenes)->ID;
        ui.Frames(false, true); // collapsed when the app quits
        size_t size = 0;
        ini = ImGui::SaveIniSettingsToMemory(&size);
    }

    DockUi relaunched(ini);
    relaunched.Frames(false, true); // still collapsed after the relaunch
    CHECK(DockUi::Node(kPreview)->Pos.x == 0.f);
    relaunched.Frames(true, true);
    REQUIRE(DockUi::Node(kScenes) != nullptr);
    CHECK(DockUi::Node(kScenes)->ID == leftId);
    CHECK(DockUi::Node(kScenes)->Size.x == 333.f);
}
