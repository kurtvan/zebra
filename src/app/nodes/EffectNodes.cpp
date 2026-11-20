#include "app/nodes/NodeDraw.hpp"

#include "app/Shell.hpp"

namespace gv::app {

void DrawEffectBody(const model::Node& node, NodeContext& context)
{
    for (const auto& spec : NodeInputs(node, context))
        context.InputRow(node, spec);

    switch (node.type)
    {
        case model::NodeType::Contrast:
        {
            float contrast = model::ParamsOf<model::ContrastParams>(node).contrast;
            if (ImGui::SliderFloat("##contrast", &contrast, 0.f, model::kMaxContrast, contrast < 10.f ? "%.2f" : "%.0f", ImGuiSliderFlags_Logarithmic))
                context.Edit(node, "Change Contrast", "contrast", [&](model::Node& n) { model::ParamsOf<model::ContrastParams>(n).contrast = contrast; });
            context.EndEditIfReleased();
            context.Tooltip("1.00 leaves the video as it is. All the way up, with Saturation at 0, gives hard black and white.\n"
                            "Double-click (or Cmd+click) to type a value.");
            break;
        }
        case model::NodeType::Saturation:
        {
            float saturation = model::ParamsOf<model::SaturationParams>(node).saturation;
            if (ImGui::SliderFloat("##saturation", &saturation, 0.f, 2.f, "%.2f"))
                context.Edit(node, "Change Saturation", "saturation", [&](model::Node& n) { model::ParamsOf<model::SaturationParams>(n).saturation = saturation; });
            context.EndEditIfReleased();
            context.Tooltip("1.00 leaves the video as it is; 0 is black and white.");
            break;
        }
        case model::NodeType::LumaGate:
        {
            const auto& gate = model::ParamsOf<model::LumaGateParams>(node);
            float threshold = gate.threshold;
            if (ImGui::SliderFloat("##threshold", &threshold, 0.f, 1.f, "threshold %.2f"))
                context.Edit(node, "Change Luma Gate", "threshold", [&](model::Node& n) { model::ParamsOf<model::LumaGateParams>(n).threshold = threshold; });
            context.EndEditIfReleased();
            context.Tooltip("Pixels darker than this are cut (brighter ones when inverted).");
            float softness = gate.softness;
            if (ImGui::SliderFloat("##softness", &softness, 0.f, 1.f, "softness %.2f"))
                context.Edit(node, "Change Softness", "softness", [&](model::Node& n) { model::ParamsOf<model::LumaGateParams>(n).softness = softness; });
            context.EndEditIfReleased();
            context.Tooltip("The cut's edge: 0 is hard, higher feathers it.");
            bool invert = gate.invert;
            if (ImGui::Checkbox("Invert", &invert))
                context.EditOnce(node, invert ? "Invert Luma Gate" : "Uninvert Luma Gate", [&](model::Node& n) { model::ParamsOf<model::LumaGateParams>(n).invert = invert; });
            context.Tooltip("Keeps the video's dark parts instead of its bright ones.");
            break;
        }
        case model::NodeType::Mix:
        {
            float opacity = model::ParamsOf<model::MixParams>(node).opacity;
            if (ImGui::SliderFloat("##opacity", &opacity, 0.f, 1.f, "opacity %.2f"))
                context.Edit(node, "Change Mix", "opacity", [&](model::Node& n) { model::ParamsOf<model::MixParams>(n).opacity = opacity; });
            context.EndEditIfReleased();
            context.Tooltip("How strongly the video goes into the input it's wired to.");
            break;
        }
        default:
            break;
    }
}

} // namespace gv::app
